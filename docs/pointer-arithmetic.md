# Pointer arithmetic, arrays and bounds

A syntax tree records `p + 1` as an addition. The EDG front end records it as pointer
arithmetic, because it has to: the integer operand scales by the size of the pointed-to type,
the result must stay inside the object the pointer came from, and the difference of two
pointers means something only when both point into the same object. The edga document keeps
what the front end decided. Pointer arithmetic has operations of its own, every array decay
is a node, and every type carries the size the scaling uses.

This page is for someone writing a checker, a data-flow engine or a query over the document.
It covers the operations, what each operand slot holds, how to turn an element count into
bytes, where an object's extent is, and a set of exports of real programs, including the
patterns a memory-safety rule has to catch. The [JSON format reference](json-format.md)
defines the fields.

Every tree on this page was produced by the edga build from this repository. The fixture
`tests/fixtures/pointers.c` and its golden file hold the first examples; the others can be
reproduced by saving the C shown and running `edga --c11 --gcc --edga-out tu.json file.c`.

## The operations

The front end has distinct operators for arithmetic over pointers, and
`tools/gen_il_names.py` derives their names from the front end's own enumeration. The ones a
memory-safety reader meets:

| Operation | Source form | What the document holds |
|---|---|---|
| `padd` | `p + n`, `n + p` | The pointer operand and the integer operand, in the order written. `pointerSecond: true` marks `n + p`. The integer operand is not converted to a pointer-sized type first. |
| `psubtract` | `p - n` | The pointer is always the first operand. |
| `pdiff` | `p - q` | The distance between two pointers, of `ptrdiff_t`'s type (`long` on LP64). Nothing in the tree says the two point into the same object; see the CWE-469 example below. |
| `padd_assign`, `psubtract_assign` | `p += n`, `p -= n` | Compound assignment over a pointer, kept apart from the integer forms. |
| `pre_incr`, `pre_decr`, `post_incr`, `post_decr` | `++p`, `p--` | The same operations as for integers; the node's type is a pointer type when the operand is a pointer. |
| `subscript` | `a[i]`, `i[a]` | The pointer and the index, in the order written; `pointerSecond: true` marks `i[a]`. An array base appears through the decay below. |
| `array_to_pointer` | implicit | The decay of an array to a pointer to its first element, as a node of its own marked `implicit`. It is what separates an array that decayed here from a pointer variable, and the array below it carries the declared extent. |
| `points_to_field` | `p->f` | Member access through a pointer; the second operand is the `field`, with its `offset`. |
| `dot_field` | `x.f` | Member access on a structure value. |
| `address_of`, `indirect` | `&x`, `*p` | As written. |

Each operation has its own correctness condition. An `add` over two integers can overflow
but cannot leave an object; a `padd` over the same values can. A rule that looks for
arithmetic that moves a pointer matches `padd`, `psubtract`, `padd_assign`,
`psubtract_assign`, and an increment or decrement whose type is a pointer, and reads
`pointerSecond` to know which operand is the pointer. It needs no type inference to do so.

## Scaling: from elements to bytes

The document does not hold the byte offset a `padd` computes; the front end scales at code
generation, not in the tree. What it holds is enough to compute that offset exactly: the type
of every expression and the `size` of every type in bytes. For `p + n` where `p` has a
pointer type whose `to` is the pointee type id, the movement in bytes is
`n * types[to].size`.

Sizes are the target's, not the reading host's. A document from a Windows (LLP64) build
gives `long` the size 4; a document from an LP64 build gives it 8. `tool.config` names the
configuration, and two documents with the same `edgCommit` and `config` agree on every size.

The same table answers how many bytes an access through a pointer touches, which is what a
bounds check needs at a `subscript` or a copy: the pointee's size times the index or count,
compared against the object's extent.

## Where the object and its extent are

An out-of-bounds access involves three facts: the pointer, the index or movement, and the
object with its extent. The document names all three.

A declared array carries its extent in its type: `int table[10]` has a type of kind `array`
with `count` 10, `size` 40 and `of` the element type. The extent is the declared one, so
`char data[16]` inside a structure is 16 whatever allocation holds the structure. A flexible
array member (`char data[]`) has an array type with `count` 0 and no `size`, and the field
is marked `flexible`: its extent is whatever the allocation left past the structure. A
declared `char data[0]` has `count` 0 and the type is marked `zeroBound`.

The path from a subscript to the array's extent runs through the decay node. In
`table[idx]` the base operand is an `array_to_pointer` over the variable `table`, and the
variable's type is the array type with its count. A reader that skips implicit nodes lands
on the variable; a reader that keeps them can tell a decayed array from a pointer parameter,
which has no extent of its own and must take one from the allocation or the caller.

The CWE-121 test case `CWE121_Stack_Based_Buffer_Overflow__CWE129_fgets_01.c` from the NIST
Juliet suite shows the whole chain. Its bad function, trimmed:

```c
int data = -1;
char inputBuffer[CHAR_ARRAY_SIZE] = "";
if (fgets(inputBuffer, CHAR_ARRAY_SIZE, stdin) != NULL)
    data = atoi(inputBuffer);
int buffer[10] = { 0 };
if (data >= 0)
    buffer[data] = 1;          /* only the lower bound is checked */
```

Exported with the compiler settings from [driving-edga.md](driving-edga.md) (status `ok`),
the write is:

```
assign : int
  [1] subscript : int (lvalue)
        [1] array_to_pointer (implicit) : int *
              [1] variable buffer : int[10]
        [2] variable data : int
  [2] constant 1 : int
```

`fgets` and `atoi` are calls with a resolved `callee`, and the assignment to `data` has the
`atoi` call as its right side. Data flow says where `data` came from, the type table says the
extent is 10, and the condition a rule checks, `0 <= data < 10`, needs nothing that is not a
field of a node the front end built.

## Examples

### Scaling and decay: `b->data + n`

From `tests/fixtures/pointers.c`:

```c
struct buffer { char data[16]; int used; };
char *advance(struct buffer *b, int n) {
  char *p = b->data + n;
  p += 2;
  p--;
  return p;
}
```

The initialiser of `p`:

```
padd : char *
  [1] array_to_pointer (implicit) : char *
        [1] points_to_field : char[16] (lvalue)
              [1] variable b : struct buffer *
              [2] field data : char[16]
  [2] variable n : int
```

The pointee is `char`, size 1, so bytes and elements coincide; over `int *` the same tree
moves 4 bytes per unit of `n`. The decay sits between the field and the arithmetic, and the
field access is a `points_to_field` over the parameter `b`, so the base object is two hops
from the function's parameter. The next two statements export as `padd_assign` over `p` and
the constant 2, then `post_decr` over `p`, both of type `char *`.

### Reversed operands: `i[values]` and `n + p`

The same fixture writes a subscript backwards, `return i[values] + values[i + 1];`, and the
document keeps the order as written:

```
subscript : int   pointerSecond
  [1] variable i : int
  [2] variable values : int *
```

`*(n + p)` over `int *p` exports as a `padd` with `pointerSecond: true` and `n` first. Both
spellings are legal C with the same meaning, and the flag says which side is the pointer, so
a reader does not have to guess from the operand types. A guess from types fails where an
integer reaches a pointer through a cast and both operands look like integers.

### Distance between two objects: CWE-469

Subtracting pointers into different objects to compute a size:

```c
size_t bad_index_in_other_string(void) {
    char first[] = "a/b";
    char second[] = "a/b";
    char *slash = strchr(first, '/');
    if (slash == 0) return 0;
    return (size_t)(slash - second);   /* undefined: different objects */
}
```

The return value exports as:

```
cast : size_t
  [1] pdiff : long
        [1] variable slash : char *
        [2] array_to_pointer (implicit) : char *
              [1] variable second : char[4]
```

C11 6.5.6p9 makes the subtraction undefined because `slash` points into `first` and the
other operand into `second`. Whether two pointers share an object is a data-flow question;
the document names the objects. `slash` is assigned a call to `strchr` whose argument is the
decayed `first`, of type `char[4]`. The correct version, `(size_t)(end - line)` with
`end = line + strlen(line)`, has the same shape, and both operands reach the same
`char line[32]`. chen's memory-safety rule MS-PTR-001 (pointer difference) works from these
facts.

### Releasing a moved pointer: CWE-761

Freeing a pointer that arithmetic moved off the start of its block is CWE-761, and chen's
rule MS-ALLOC-006 reports it. Two cases with the same syntax and opposite verdicts:

```c
void good_free_inline_header(void *p) { free((hdr_t *)p - 1); }   /* header allocator */
void bad_free_plus_one(size_t n) { char *p = (char *)malloc(n); free(p + 1); }
```

The first argument of `free` in the first function is a `psubtract` of type `hdr_t *` over
an explicit cast of the parameter `p`; in the second it is a `padd` of type `char *` over a
variable assigned the result of `malloc`. Both calls resolve to the declared `free`. chen
records each operation as a `ptr-arith` tag on the graph node, `add:<i>` or `sub:<i>` with
the argument index of the pointer operand (`p + n` is `add:1`, `n + p` is `add:2`), or `diff`,
and the rule reads that tag next to the allocation facts instead of working out again which
operand was the pointer. What separates the two cases is where the pointer came from: a
parameter whose contract places a header before it, or a fresh allocation.

## A query over one document

With the type table, these questions become lookups. The script below lists every
operation that moves a pointer, with its position, and every subscript whose base is a
declared array: a constant index outside the extent is reported as a defect, and any other
index is printed with the bound it must respect, which is the worklist a taint engine starts
from.

```python
import json
import sys

doc = json.load(open(sys.argv[1]))
types = {t["id"]: t for t in doc["types"]}
POINTER_OPS = {"padd", "psubtract", "pdiff", "padd_assign", "psubtract_assign",
               "pre_incr", "pre_decr", "post_incr", "post_decr"}


def nodes(n):
    """Every expression and statement under n, depth first."""
    if isinstance(n, dict):
        yield n
        for v in n.values():
            yield from nodes(v)
    elif isinstance(n, list):
        for v in n:
            yield from nodes(v)


def is_pointer(n):
    return types.get(n.get("t"), {}).get("kind") == "pointer"


def declared_array(base):
    """The array type behind a subscript base that is a decayed array, else None."""
    if base.get("op") != "array_to_pointer" or not base.get("ops"):
        return None
    t = types.get(base["ops"][0].get("t"), {})
    return t if t.get("kind") == "array" and t.get("size") else None


for routine in doc["routines"]:
    for n in nodes(routine.get("body")):
        if n.get("k") != "operation":
            continue
        op, ops = n.get("op"), n.get("ops", [])
        where = routine["qualifiedName"]
        if n.get("p"):
            where += f":{n['p'][1]}:{n['p'][2]}"
        if op in POINTER_OPS and (not op.endswith(("incr", "decr")) or is_pointer(n)):
            print(where, op, "pointerSecond" if n.get("pointerSecond") else "")
        elif op == "subscript" and len(ops) == 2:
            base, index = (ops[1], ops[0]) if n.get("pointerSecond") else (ops[0], ops[1])
            array = declared_array(base)
            if array is None:
                continue
            count = array["count"]
            if index.get("k") == "constant" and index.get("ck") == "integer":
                value = int(index["value"], 0)
                if not 0 <= value < count:
                    print(where, "constant index", value, f"outside [0, {count})")
            else:
                print(where, "index", index.get("name", index.get("op")), f"needs 0 <= i < {count}")
```

On the Juliet unit above it prints, among the good functions' lines:

```
CWE121_Stack_Based_Buffer_Overflow__CWE129_fgets_01_bad:49:19 index data needs 0 <= i < 10
CWE121_Stack_Based_Buffer_Overflow__CWE129_fgets_01_bad:53:36 index i needs 0 <= i < 10
```

On `pointers.c` it prints the `pdiff` in `distance` and the `padd`, `padd_assign` and
`post_decr` in `advance`; and on `int table[8]; return table[8];` it prints
`constant index 8 outside [0, 8)`. A flexible member has no `size`, so the script leaves it
to the allocation.

## Compared with a syntax tree

The [EDG and CDT comparison](edg-versus-cdt.md) covers the two designs. As far as this page
goes: a syntax tree records `p + 1` as an addition and types its operands on request, so a
reader first has to work out that one operand is a pointer, which one, and what the scaling
is, and in template-heavy C++ that inference can come back empty. The document's operations
carry those facts because the front end needed them to check the program. The cost is that
the front end must accept the unit: a translation unit whose headers are missing is not
exported, where a tolerant parser returns a tree with gaps. chen offers both, and atom's
`--c-frontend edg-fallback` uses EDG with CDT for the files edga cannot export.

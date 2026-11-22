# Memory-safety facts in the document

A memory-safety checker asks questions the source text does not answer directly. How many
bytes does this object own? What does this conversion discard? When does this temporary die?
Which function did this library call resolve to? The EDG front end answers each of these in
order to check and compile the program, and the edga document keeps the answers as fields of
the nodes they belong to. This page lists them for an analyst writing or reading rules, each
with an export and the place in chen that uses it. The
[pointer arithmetic page](pointer-arithmetic.md) covers the operations that move a pointer,
and the [JSON format reference](json-format.md) defines every field.

The rules named below are chen's memory-safety rules, defined in
`MemorySafetyFindingPass` in chen's x2cpg module and reported by atom's `memory-safety`
command under their `MS-*` ids. The tags are the ones chen's edg2atom frontend writes from
these fields (`io.appthreat.x2cpg.Defines`). Every fact here can be checked against the raw
JSON without chen. The trees were produced by the edga build from this repository, from the
fixtures in `tests/fixtures` where one is named and otherwise from the C shown.

## Conversions are nodes

Every implicit conversion is a `cast` operation marked `implicit: true`. Its single operand is
the expression converted, and the node's own `t` is the type converted to. An explicit cast
has the same shape without the flag. In

```c
void bad_signed_to_size(int n, const char *src) {
    char dst[64];
    memcpy(dst, src, (size_t)n);
}
```

the third argument of `memcpy` is an explicit `cast` of type `size_t` over the variable `n`
of type `int`. In `pointers.c`, `unsigned char c = n;` over an `int` `n` is an implicit `cast`
of type `unsigned char` over an operand of type `int`, and the type table gives the widths,
4 and 1. The sign change and the narrowing are each a node, not something a reader must work
out by comparing two type names.

chen summarises each implicit conversion between arithmetic types in an
`implicit-conversion` tag, `<from>-><to>:<kind>`, where the kind is `narrowing`,
`sign-change`, `promotion` or `arithmetic`. chen's `IntegerWidthPass` reads those tags, and
the integer rules use the result: MS-INT-001 to MS-INT-004 (overflow and underflow),
MS-CONV-001 (narrowing conversion), MS-CONV-002 (sign extension to a size) and MS-ALLOC-008
(uncontrolled allocation size). chen's CDT frontend writes the same tag from CDT's own types;
from edga the conversion is the one the front end applied.

## Object extents

`char dst[64]` is a variable whose type has kind `array`, `of` `char`, `count` 64 and `size`
64. The extent reaches every use: a copy's argument, a `subscript` base, and a `sizeof` the
front end folded, because a folded constant keeps the expression it came from. `sizeof(p->payload)`
over `struct pkt { int len; char payload[16]; }` exports as the constant 16 whose `expr` is
the `sizeof` over the `points_to_field`, and that field carries `offset` 4. A structure's
`fields` give each member's byte `offset`, so the extent of `pkt->payload` is its array
type's count and its distance from the object's start is the member's offset.

An out-of-bounds read joins two such facts. In `int table[10] = {0}; printf("%d\n",
table[idx]);` with `idx = atoi(input)`, the extent 10 is on the variable's type and the
`atoi` call is on the assignment to `idx`; data flow connects them. chen's MS-BOUND-003 and
MS-BOUND-004 (index read and write) are that join, and MS-BOUND-006 and MS-BOUND-007 check
copies against fixed and heap extents. chen writes `object-size` and `pointee-size` tags from
the type table so the rules do not have to go back to it.

## Layout: offsets, bit-fields, flexible members

Every field of every structure and union in `types` has its byte `offset`. A bit-field adds
`bitField: true`, `bits` (its width) and `bitOffset` (its first bit within the byte at
`offset`, left out when 0). The last member, when it is an array of unknown bound, is marked
`flexible`; a declared `[0]` array has an array type marked `zeroBound`. These are the
target's values, the ones `offsetof` and `sizeof` give, not the reading host's.

Compared against the host compiler, both run on arm64 macOS:

```c
struct bits {
  unsigned a : 3;
  unsigned b : 5;
  unsigned : 0;   /* closes the storage unit */
  unsigned c : 7;
  unsigned d : 11;
  signed e : 5;
};
struct tail_flex { unsigned n; char data[]; };
struct tail_zero { unsigned n; char data[0]; };
```

The compiler prints `sizeof` 8, 4 and 4, and `offsetof(struct tail_flex, data)` 4. The
document gives `struct bits` `size` 8 with these fields:

| Field | `offset` | `bits` | `bitOffset` |
|---|---|---|---|
| `a` | 0 | 3 | |
| `b` | 0 | 5 | 3 |
| (unnamed) | 4 | 0 | |
| `c` | 4 | 7 | |
| `d` | 4 | 11 | 7 |
| `e` | 6 | 5 | 2 |

So `d` covers bits 7 to 17 counted from byte 4, ending in byte 6, which `e` shares. That is
what a rule about overlapping writes or packed structures needs.

`struct tail_flex` has `size` 4, with `data` at offset 4 marked `flexible` and an array type
of `count` 0 without a `size`: the bytes past offset 4 belong to the allocation, so a bounds
rule checks writes to the member against the size passed to the `malloc` that produced the
object. chen tags such a member `flexible-member`. `struct tail_zero` has the same `size`
and offset; its `data` is not marked `flexible`, and its array type is marked `zeroBound`
instead, so the declared spelling is kept.

## Lifetimes of temporaries

An initialiser whose object is destroyed at the end of something says what that is in
`lifetime` (`fullExpression`, `block`, `try`, `static`) and names the routine in
`destructor`. From `tests/fixtures/lifetimes.cpp`:

```c++
void temporary_ends(int id) {
  const char *p = make(id).c_str();   /* the temporary dies at the ';' */
  use(p);                             /* p dangles here */
}

void reference_extends(int id) {
  const text &kept = make(id);        /* the reference keeps it alive */
  use(kept.c_str());
}
```

The temporary in the first function is a `temp_init` whose initialiser has
`lifetime: fullExpression` and the destructor `text::~text`; in the second the initialiser has
`lifetime: block`, because binding the temporary to a reference extends it to the end of the
block. chen turns destructor calls at scope ends into call nodes tagged `compiler-generated`
and `lifetime-end`, which gives a use-after-scope rule a node at which the lifetime ends.

A compound literal, `int *pair = (int[]){x, 0};` in `tests/fixtures/layout.c`, exports as a
`temp_init` whose initialiser is marked `compoundLiteral`, of type `int[2]`. Its lifetime is
the enclosing block (C11 6.5.2.5p5), and its type is an extent like any declared array's:
chen's index rules check `pair[1]` against the count 2 of the literal `pair` was assigned.

## Calls resolved to what they call

A call names the routine it resolved to in `callee`. `tests/fixtures/fortify.c` reproduces
what C libraries do under `_FORTIFY_SOURCE`:

```c
#define memcpy(d, s, n) __builtin___memcpy_chk(d, s, n, __builtin_object_size(d, 0))

void fill(struct packet *p, const char *src, size_t n) {
  memcpy(p->data, src, n);
}
```

The statement exports as a call to `__builtin___memcpy_chk` whose fourth argument is a call
to `__builtin_object_size` over the decayed `p->data`, of type `char[16]`: the check the
compiler would generate is in the tree, and the destination flows into it. The program's own
spelling is not lost. Every node of the call has `mi`, the macro invocation it came from, and
that entry of `macroInvocations` is `memcpy` at the source position.

Allocation and release resolve the same way. `malloc` and `free` are calls to the declared
functions. A `new_delete` node with `new: true` names the type it allocates in `of` and the
allocation function in `routine` (`operator new`, `operator new[]`, or a placement form with
`placement: true` and its `args`). One with `new: false` is a `delete`: `array` marks
`delete[]`, `routine` is the release function where one is declared, and for a class type its
`init` names the `destructor` that runs. chen's MS-ALLOC-001 (double free), MS-ALLOC-003
(leak), MS-ALLOC-005 (freeing memory not on the heap) and MS-ALLOC-007 (mismatched release)
pair these with the allocation-state facts.

## Constants remember where they came from

The front end folds constants and keeps the expression it folded, in `expr`. `sizeof` above is
one case; `unsigned int u = -1;` exports as the constant 4294967295 over an implicit `cast`
over the constant -1 over a `negate`, so the wraparound is visible. An enumerator used in an
expression or a `case` label folds to its value and keeps its `name` and, when that is
unambiguous, where it was `declared`; the top-level `enumerators` table maps the file- and
namespace-scope enumerators the unit declares to their positions. A rule over `case LIB_BAD:`
can therefore point to the header that defines `LIB_BAD`, not only the switch that uses it.

## Macro provenance

Every node from expanded code carries `mi`, the id of its innermost macro invocation, and
`po`, where its text was written: in the macro's definition or in an argument at the
invocation. The `macroInvocations` table holds the whole expansion tree with parents, and the
[architecture page](how-edga-works.md#positions-and-macro-provenance) rebuilds nested
invocations from it. For a taint rule this matters when a length reaches a copy through a
macro argument: the length's node points at the argument's own text, not only at the
invocation. chen represents each invocation as a call with copies of its arguments and tags
nodes `macro-invocation`, `macro-parent`, `macro-origin` and `macro-argument-copy`; atom's
callsites slice reads those to attribute a use to the header that defined the macro.

## What a syntax-only reader has to rebuild

The sections above are also the list of what a reader working from syntax alone must
reconstruct: conversion widths and signedness, object extents, offsets and bit positions,
where temporaries and scopes end, the real callee behind a wrapper, the origin of a folded
constant, and the macro expansion a node came from. Each reconstruction can miss a fact,
which loses findings, or get one wrong, such as picking the wrong pointer operand when both
look like integers, which adds false ones. The front end has already computed these facts
to compile the program, and a checker can read them from the document.

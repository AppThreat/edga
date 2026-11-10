# The edga JSON format

edga writes one JSON object per translation unit. This page describes that object for someone
writing a consumer: the tables, how they refer to each other, the tree of a function body, and
what stays stable between edga versions. `schema/edga-1.schema.json` is the precise contract,
with a description on every field that needs one; the golden tests validate every document they
produce against it, and `tests/expected` holds complete examples for small programs.
[How EDG and edga work](how-edga-works.md) explains where the facts come from.

## Versions and stability

The `schema` field is `"edga/1"`. Within version 1, a new edga release may add tables, fields,
kinds and operator names, and a consumer should ignore what it does not know. A change that
removes or renames a field, or changes what a field means, gets a new schema version. The kind and
operator names come from the EDG front end's own enumerations, so an update of the EDG commit can
add new ones; that is an addition, not a break.

The `tool` object says which build wrote the document: the exporter `version`, the `edgCommit` it
was built from, and `config`, the front end configuration's name with a fingerprint of its
settings (`linux-x86_64-<hash>`). Two binaries with the same `edgCommit` and `config` interpret the
language the same way; chen uses the whole `--edga-version` output as part of its cache key.

## The top level

| Field | What it holds |
|---|---|
| `schema` | `"edga/1"`. |
| `tool` | `name`, `version`, `edgCommit`, `config`. |
| `tu` | The unit: `path`, `language` (`c` or `c++`), `status`, and `mode` (`gcc`, `gpp`, `clang`, `microsoft`: which compiler the front end emulated). |
| `files` | Every file the unit read, in include order. |
| `macros` | Every macro defined while the unit was read, with its definition text. |
| `macroInvocations` | Every macro expansion, as a tree. |
| `inlineNamespaces` | The unit's inline namespaces, qualified (`std::__1`). |
| `globals` | Variables of file and namespace scope, and static data members. |
| `routines` | Functions: their signature, attributes and, when exported, their body. |
| `types` | Every type the other tables refer to. |
| `enumerators` | File- and namespace-scope enumerators by name, each with where it is declared. |
| `diagnostics` | The front end's warnings and errors. |

`tu.status` is `ok` when the front end reported no errors, `errors` when it reported some (the
document holds the parts it could build), and `failed` when the front end stopped before its
back end ran, for example at an `#error` directive. A failed document has only `schema`, `tool`,
`tu` (without `language` and `mode`) and `diagnostics`.

## Ids and positions

Files, macros, macro invocations, variables, routines and types have an integer `id`, unique
within its table (a variable and a routine can share a number); ids are not stable across
documents. Other entries
refer to them by id: a variable's `type`, a call's `callee`, a file's `includedFrom`. Ids are
assigned as entries are written, so do not assume they are dense or ordered.

A position is `[file id, line, column]`, with lines and columns starting at 1 and file 0 the
primary source file. It can be `null` when the front end has no position, as for some implicit
nodes. Nodes carry `p`, where the node is; declarations also carry `start`, where the declaration
starts, and statements and expressions carry `end` or `range` (the first and last token).

A node that came from a macro expansion has `p` at the top-level invocation in the source as the
compiler sees it, `mi` with the id of the innermost invocation it was expanded in, and `po`, where
its text was written: in the macro's definition or in an argument at the call site. Each entry in
`macroInvocations` has `macro` (the macro's id), `name`, `start`, `end` and `parent`, the
invocation it appears in (0 for none), so nested expansions can be rebuilt. The
[worked example](how-edga-works.md#positions-and-macro-provenance) shows the three positions
for nested macros.

## Files and macros

A file has `path` (as the front end opened it), `written` (as the `#include` spelled it),
`includedFrom` (`[file id, line]` of the include), `system` (it came from a system include
directory), `systemInclude` (it was included with angle brackets), `inRoot` (it is under
`--edga-root`) and `lineDirective` (a `#line` directive named it). `system` is the reliable way to
tell project code from the C library and toolchain headers.

A macro has `name`, `objectLike`, `text` (the whole `#define` line), `p`, and `undef` when it was
later undefined.

## Variables and routines

A variable has `name`, `qualifiedName`, `type`, `storage` and `linkage` (the front end's names,
such as `static`, `extern`, `auto`, `external`, `internal`, `cplusplus_external`, `none`), and
`init`, its initialiser. Flags such as `addressTaken`, `modified`, `implicit` and `this` are present
only when true. Structured bindings have `bindingContainer` on the unnamed object and `bound` on
each binding.

A routine has `name`, `qualifiedName`, `type` (its routine type), `returnType`, `params` (each a
variable), `linkage`, `storage`, `attributes` and `start`. C++ adds `class`, `special`
(`constructor`, `destructor`, `conversion`, `operator`, ...), `operator`, `virtual`, `pureVirtual`,
`inline`, `templateInstance`, `lambda` and `ctorInits`. `defined` says whether the unit defines it;
only routines defined in files under `--edga-root` (or in any header with `--edga-headers`) have a
`body`. Others are listed with their declaration so calls to them still resolve.

The routines table is not limited to definitions: every routine a call names is written, even
when it is declared in a header or in a scope the first walk did not reach.

## Bodies: statements, expressions and initialisers

A body is a tree. Every node has `k`, its kind, which is the front end's kind name without its
prefix (`stmk_`, `enk_`). Statement kinds include `block` (with `stmts`), `decl` (with `decls`, a
list of variables), `expression` (with `expr`), `if` (`cond`, `then`, `else`), `while`,
`end_test_while`, `for` (`init`, `cond`, `inc`, `body`), `range_based_for` (`variable`, `over`,
`body`), `switch` and `switch_case`, `return`, `goto` and `label`, `try_block` (with `handlers`),
`asm` (`text`, `operands`) and `set_vla_size` (`arrayType`, `size`).

An expression has `k`, `t` (its type id), `range`, and `lv` when it is an lvalue. The common kinds
are:

| `k` | Meaning and fields |
|---|---|
| `operation` | An operator applied to `ops`. `op` names it (`add`, `assign`, `call`, `cast`, `padd`, `subscript`, `points_to_field`, `question`, ...). A call has `callee`, the routine id, when it resolved to one; `virtual` and `operatorSyntax` say how it was called. |
| `variable` | A use of the variable `var`, with its `name`. |
| `routine` | A use of the routine `routine`, with its `name`. |
| `field` | A member, by `name`. |
| `constant` | A value: `ck` is its kind (`integer`, `float`, `string`, `address`, `aggregate`, `designator`, `dynamic_init`, ...), `value` the value as text. A constant the front end folded keeps the expression it came from in `expr`, and a folded enumerator keeps its `name` and, when unambiguous, where it was `declared`. |
| `new_delete` | `new` or `delete`: `new`, `array`, `of` (the type allocated), `routine` (the allocation function), `init`. |
| `temp_init` | A temporary, with its `init`. |
| `lambda` | A lambda expression; its initialiser lists its `captures`. |
| `sizeof`, `condition`, `throw`, `statement` | As named, with `expr` where there is an operand. |

Nodes the front end made, such as implicit conversions, `!= 0` tests and implicit `this`, have
`implicit: true`. A call argument supplied by a default argument has `defaultArgument: true`.

An initialiser also has a `k` (`expression`, `constructor`, `constant`, `zero`, `bitwise_copy`,
`nonconstant_aggregate`, ...) with `expr`, `routine` and `args`, or `const`. When the initialised
object is destroyed at the end of something, `lifetime` says what (`fullExpression`, `block`,
`try`, `static`) and `destructor` names the routine. `compoundLiteral` marks the unnamed object of
a compound literal, and a lambda's `captures` list each capture's closure `field`, the captured
`var` (or `this`), `byReference`, `implicit` and an init-capture's `initializer`.

## Types

A type has `id`, `kind`, `name` (spelled as the front end prints it in diagnostics) and `size` in
bytes. The other fields depend on the kind:

| `kind` | Fields |
|---|---|
| `integer`, `float` | `signed`, `bool`, `enum`; an enum has `tag`, `scoped` and `enumerators` (`name`, `value`, `p`). |
| `pointer` | `to`, and `reference` or `rvalueReference` for C++ references. |
| `array` | `of`, `count`, `variableSize`, and `zeroBound` for a declared `[0]` (an unknown bound also has `count` 0). |
| `routine` | `returns`, `params`, `variadic`. |
| `class`, `struct`, `union` | `tag`, `qualifiedName`, `incomplete`, `span`, `fields` (`name`, `type`, `offset`, and `bits`, `bitOffset` and `flexible` where they apply), `bases`, `closure` for a lambda's closure type, `templateInstance`. |
| `typeref` | A typedef: `typedef` is its name and `to` the type it names. |
| `ptr_to_member`, `vector`, `nullptr`, `complex`, ... | The remaining front end type kinds, with `to` or `of` where they point at another type. |
| `void`, `error` | Nothing more; `error` stands for a type the front end could not determine. |

Type names include inline namespaces as the front end prints them
(`std::__1::vector<int, std::__1::allocator<int>>`); `inlineNamespaces` lists them so a consumer
can also match the shorter spelling a program uses.

## Diagnostics

Each diagnostic has `level` (`warning`, `error`, ...), `code` (the front end's, such as `EC20`),
`message`, and `file`, `line` and `column`. `file` is a `file://` URI, as SARIF writes it.

## Reading a document

The tables make most questions a lookup. This prints every call in the unit with the routine it
resolved to:

```python
import json

doc = json.load(open("tu.json"))
routines = {r["id"]: r for r in doc["routines"]}

def calls(node):
    if isinstance(node, dict):
        if node.get("k") == "operation" and node.get("op") == "call" and "callee" in node:
            yield node["p"], routines[node["callee"]]["qualifiedName"]
        for value in node.values():
            yield from calls(value)
    elif isinstance(node, list):
        for value in node:
            yield from calls(value)

for routine in doc["routines"]:
    for position, callee in calls(routine.get("body")):
        print(routine["qualifiedName"], "calls", callee, "at", position)
```

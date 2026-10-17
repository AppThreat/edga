# EDG and Eclipse CDT compared

chen builds its C/C++ code property graphs with one of two front ends. The default,
`c2cpg`, parses with Eclipse CDT inside the JVM. The alternative, `edg2atom`, runs
edga, the exporter around the EDG front end, and reads the JSON it writes. Both
produce the same graph shapes, run under the same configuration and compilation
database support, and feed the same passes and queries, which is what makes a
comparison meaningful: the differences that survive are the differences in what each
parser knows.

This document explains where the two designs come from, how their architectures
differ, and what each is good at. It is grounded in the two parsers' sources and in
chen's `GoldenComparisonTests`, which build the graph of the same fixtures with both
front ends and list every reviewed difference.

## What Eclipse CDT is

Eclipse CDT (C/C++ Development Tooling) is the C and C++ language support of the
Eclipse platform: the editor, navigation, build integration, debugger front end and
the parser underneath them all. Its parser's initial API and implementation date from
2002 and were written at IBM; the file headers record contributors from QNX, Wind
River, Nokia, Symbian, Google, IFS and Ericsson over the years since. It is licensed
under the Eclipse Public License 2.0. chen bundles the `org.eclipse.cdt.core` jar
(version 9.3.200, from the CDT 12.6.0 release) as a plain library in
`platform/frontends/c2cpg/lib`, without the rest of the IDE.

Everything about CDT follows from its job: it is IDE infrastructure. A parser behind
an editor must never say nothing (the user is mid-edit, the code does not compile), so
the DOM parser is tolerant by construction and always returns a best-effort syntax
tree. It must answer small questions quickly (is the cursor inside a function? what
is at this offset?), so it has parse modes for that (`ParserMode.QUICK_PARSE`,
`STRUCTURAL_PARSE`, `COMPLETE_PARSE`, `COMPLETION_PARSE`, `SELECTION_PARSE`) and
options to skip function bodies, skip trivial expressions or parse code under `#if 0`
(`ILanguage.OPTION_*`). And it must scale to a workspace without compiling it, so
semantic resolution is lazy: names resolve bindings on demand
(`IASTName.resolveBinding()`), failures come back as `IProblemBinding` rather than
errors, and cross-file knowledge lives in an optional index (the PDOM), which chen
leaves off by default.

## What the EDG front end is

The EDG front end is the other kind of thing: a compiler front end, developed by
Edison Design Group over nearly four decades and licensed into compilers and analysis
tools (the open-sourcing announcement names Veracode's static analysis and
MathWorks' instrumentation among them). Its source was published on September 30,
2026 at [github.com/edgcpp/compiler](https://github.com/edgcpp/compiler), Apache
License 2.0 with LLVM exceptions. Its design goal is the compiler's goal: accept the
program exactly as the project's compiler accepts it (with explicit GCC, Clang and
MSVC emulation modes aimed at bug-for-bug compatibility), and by the end of parsing
have a complete, checked, resolved view of the translation unit: the IL. There is no
lazy path through it; a name either resolved or the front end emitted a diagnostic.

edga packages that front end as a standalone binary that exports the IL of one
translation unit as JSON (see [how-edga-works.md](how-edga-works.md) for the
internals). chen's `edg2atom` runs it once per translation unit and builds the graph
from the document.

## Architectures side by side

CDT runs in process, on top of an abstraction it defined for IDE use; EDG runs out of
process, on an IL defined for code generation. The two pictures:

```
c2cpg (CDT)                                      edg2atom (EDG)
=============                                    ===============

chen JVM                                         chen JVM
  └─ AstCreationPass                              └─ EdgAstCreationPass
       └─ CdtParser                                    └─ EdgaRunner
            └─ GPPLanguage / GCCLanguage                    └─ subprocess: edga
                 ├─ IScanner (preprocessor)                      └─ EDG front end
                 ├─ GNUCPPSourceParser (or the C one)                ├─ preprocessor
                 │    hand-written recursive descent,                 ├─ parser + semantics
                 │    tolerant, IDE-shaped                            ├─ templates instantiated
                 └─ DOM AST: IASTTranslationUnit                      └─ IL walked by back_end()
                      │                                                   └─ JSON "edga/1"
                 lazy semantics:                                    EdgaUnit parses the JSON
                 resolveBinding(), getExpressionType()               into tables and bodies
                 failures: IProblemBinding
```

The load-bearing differences:

Semantics are eager in EDG and lazy in CDT. When edga writes a call, the callee is
already resolved to a routine and the expression already has a type, because the IL
was built for a back end that generates code from it. CDT's DOM tree is syntactic;
`c2cpg` asks for bindings and expression types where it wants them, and when
resolution does not succeed (heavy templates, unusual constructs) it gets a problem
binding or nothing and writes the type it has. The golden differences below are
mostly this one design decision surfacing.

Templates are instantiated by EDG and described by CDT. EDG instantiates the
templates a unit uses (`--instantiate used`), so `Box<int>` exists as a class with
`int` fields and a call links to `max_of:int(int,int)`. CDT keeps the template
definition; an instantiation is computed during binding resolution if it can be, and
in chen's graphs template members frequently come out with unresolved types. edg2atom
then has a problem CDT does not have: every unit that includes the header writes the
instance again, so `DuplicateDefinitionPass` keeps one copy.

The process boundary is where each side gets its operational character. CDT parses
inside chen's JVM: no serialisation, direct object access, and the AST cache shares
the heap. But a CDT parse that runs too long can only be cancelled cooperatively,
through a log-service hook (CDT bug 226682; the parser does not poll
`Thread.interrupt()`), which is why chen wraps it in a `CancelableLogService`. edga is
a separate process: chen gives it ten minutes per unit and kills it outright
(`destroyForcibly`) if it exceeds that, and a unit's memory lives and dies with its
process rather than the JVM heap. The JSON boundary in between costs serialisation and
parsing, and buys isolation and a written contract (`schema/edga-1.schema.json`) that
survives edga and chen versions independently.

Failure behaviour differs in kind. EDG with errors still runs the back end (edga's
configuration insists on it), so a broken unit exports its working parts with
`"status": "errors"` and its diagnostics, codes included, in SARIF; only a front end
that gives up entirely produces `"status": "failed"`. CDT always produces a tree,
wrong code included, and reports problems as `IProblemBinding`s the frontend counts.
In practice this is why the two front ends are complementary in chen: units EDG
cannot digest at all (and the headers they own) fall back to CDT, which will return
something for nearly anything.

Macro provenance is first-class in both, differently shaped. CDT records image
locations: for each name, where it came from, including macro expansions
(`IASTMacroExpansionLocation`), and `c2cpg`'s `MacroHandler` reconstructs invocations
from them, writing INLINED calls whose arguments are copies of the subtrees the
arguments became. EDG (built with `MACRO_INVOCATION_TREE_IN_IL=1`) hands edga the
invocation tree itself: every invocation with its parent, and every node carrying
both its innermost invocation and where its text was written (definition or
argument). `edg2atom`'s `MacroCalls` writes the same INLINED call shapes from that
tree, with less reconstruction and more certainty.

Compiler emulation is EDG's own feature. The front end has modes that emulate GCC,
Clang and MSVC at specific versions, dialect quirks included, and chen selects them
from the project compiler's predefined macros. CDT offers GNU-dialect language
objects (`GCCLanguage`, `GPPLanguage`) with scanner extension configurations;
`c2cpg` configures them with the compiler's macros and include paths gathered the
same way (the `PredefinedMacros` machinery is shared, and `edg2atom` reuses it).

## What the difference means in the graph

chen's `GoldenComparisonTests` (fixtures under
`platform/frontends/edg2atom/src/test/resources/golden`) parses the same files with
both front ends and records every difference that is not on the reviewed list. The
reviewed differences are concrete illustrations of the design points above.

Types resolve where they used not to. An enumerator in C is an `int`; the CDT
frontend leaves `RED` untyped, edg2atom types it `int`. A character constant in C is
an `int`; the CDT frontend calls it `char`. A lambda's parameter declared `int`
resolves as `int` instead of `ANY`.

Template instances are real. Where the CDT frontend writes one `max_of` with `T` and
`ANY`, edg2atom writes `max_of:int(int,int)` and `max_of:long int(long int,long int)`
and points each call at the instance it uses. `Box<int>.get:int()` replaces
`Box.get:ANY()`, and the member `v` is typed `Box<int>` instead of unresolved.

Implicit code appears. A constructor's member initialisers (`w_(w)`) are stores to
`this->w_`; the CDT frontend writes the constructor without them. A braced return
value (`return {x + o.x, y + o.y};`) becomes an array initialiser of its elements;
the CDT frontend writes the return without a value.

Spelling differs harmlessly. `long` as edga spells it against `long int` as CDT does;
the passes normalise both.

Beyond the golden list, edg2atom turns what the front end knows into tags on the
graph (`io.appthreat.x2cpg.Defines`): `implicit-conversion` on front-end-made
conversions, `virtual-call` on virtual dispatch, `default-argument` on arguments a
declaration's default supplied, `ptr-arith` on pointer arithmetic with the operand
that was the pointer, `operator-call` on calls that use operator syntax, `vla-size`,
`compiler-generated`, `lifetime-end`, `const-value`, `alloc-form`, `fn-attr`, and
`macro-invocation` and `macro-origin` on macro-expanded nodes. These are facts a
syntax tree does not have.

## Relative strengths

EDG's strength is precision. Every expression has a type, every call a target,
implicit behaviour is explicit and attributed, constants are folded but keep the
expression they came from, and the whole view is the one a compiler would generate
code from. It is also self-describing about failure: per-unit status, SARIF
diagnostics, and a schema for the document. Its emulation modes mean the analysis
sees the program as the project's compiler sees it, and its process isolation makes
resource limits real: a unit that runs away is killed, and the run says so.

CDT's strength is resilience and integration. It is a Java library already inside
chen: no second binary to distribute, no per-unit process, no JSON on the path, and
direct access from AST node to graph node. It was built to parse code that does not
compile, mid-edit, which makes it the safety net under `--c-frontend edg-fallback`,
and its optional behaviours (parsing inactive preprocessor branches, skipping
bodies) are knobs an IDE-shaped analysis can use. It has been chen's default for
years and carries the corresponding hardening: the fixtures, the shims and the graph
shapes all grew up around it, to the point that edg2atom's stated contract is to
reproduce its output shapes and add what the front end knows.

The honest costs on each side: EDG via edga pays a process launch and a JSON
serialisation per translation unit, needs its compiler-emulation inputs to be right
(hence the host shims in `EdgaRunner`), and materialises template instances that
then need deduplication. CDT's lazy resolution leaves gaps precisely where modern
C++ is densest, its cancellation is cooperative rather than real, and its semantics
are those of an IDE index rather than a compiler.

## Choosing between them

atom's `--c-frontend` flag names the choice: `cdt` (the default), `edg`, or
`edg-fallback` (edg, with CDT for whatever edga cannot export; every FILE node says
which frontend built it). The practical guidance follows from the above. Use the
default when in doubt: it is the most exercised path and has no external
dependencies. Prefer `edg` when the analysis leans on types and resolution, template
heavy C++ in particular, when macro provenance matters, or when per-unit isolation
and timeouts matter more than per-unit overhead. Use `edg-fallback` for broad
coverage of messy code with edg's precision where it applies. In all three cases the
graph, the passes and the queries are the same objects.

## Summary

| | Eclipse CDT (`c2cpg`) | EDG front end (`edga`, `edg2atom`) |
|---|---|---|
| Origin | IDE language support for Eclipse, since 2002 (IBM and others) | Compiler front end from Edison Design Group, open sourced September 2026 |
| Licence | Eclipse Public License 2.0 | Apache 2.0 with LLVM exceptions |
| Implementation | Java, in chen's JVM, jar bundled in `c2cpg/lib` | C sources compiled as C++ (about a million lines under `src/`), pinned submodule, built unmodified into the edga binary |
| Representation | DOM AST (`IASTTranslationUnit`), hand-written tolerant recursive descent | IL, a high-level tree built for code generation, source correspondence and macro provenance included |
| Semantics | Lazy binding resolution, `IProblemBinding` on failure | Eager and complete: types, resolutions, implicit code, folded constants with provenance |
| Templates | Definitions described, instances computed lazily during resolution | Used instances instantiated per unit with concrete types, deduplicated by a pass |
| Macros | Image locations per name, invocations reconstructed | Invocation tree with parents, per-node innermost invocation and original position |
| Compiler dialects | GNU dialect language objects, scanner extension configurations | Emulation modes for GCC, Clang and MSVC at a version, bug-for-bug goal |
| Errors | Best-effort tree always returned | Partial export with `"status": "errors"` and SARIF diagnostics; `failed` only when the front end gave up |
| Process model | In-process, threads of the JVM, cooperative cancellation | One subprocess per translation unit, ten minute timeout, hard kill, JSON document over a schema |
| In chen | The default frontend, shapes and fixtures grew around it | The alternative; falls back to CDT per file with `edg-fallback`; FILE nodes tagged with the frontend that built them |

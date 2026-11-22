# Driving edga as the project's compiler

The EDG front end reads a translation unit the way the project's compiler does only when it
is given the same dialect, language standard, predefined macros and include directories.
chen's `EdgaRunner` works these out from a compilation database and the host compiler. This
page shows how to do the same by hand, for someone wiring edga into a build, a CI job or an
investigation of one unit. The commands were run as written on macOS with Apple clang 21.0.0;
on Linux the same steps apply with the paths the local compiler reports.
[How EDG and edga work](how-edga-works.md#driving-the-front-end-as-the-projects-compiler)
summarises what chen does with the same options.

## The shape of a run

One process reads one translation unit and writes one document:

```bash
edga [front end options...] --edga-out tu.json --edga-root <project dir> file.c
```

`edga --edga-help` lists edga's own options; every other option goes to the front end. The
exit status follows the unit's status: 0 for `ok`, 2 for `errors` (the document is written
with the parts the front end could build), and 4 for `failed` (the front end stopped before
the back end ran, for example at an `#include` it cannot open, and the document holds only
the diagnostics). A primary file that cannot be read exits with 4 and writes no document. A
driver that treats 2 as fatal throws away usable units on most real code bases; the useful
test is whether a document was written and what its `status` says.

## Dialect and version

The front end emulates GCC, Clang and MSVC: `--gcc` (C) or `--g++` (C++) with
`--gnu_version`, `--clang` with `--clang_version`, and `--microsoft` with
`--microsoft_version`. The GNU and Clang versions are one integer, major times 10000 plus
minor times 100 plus patch, so Apple clang 21.0.0 is 210000 and GCC 13.3.0 is 130300. MSVC's
is `_MSC_VER` as the compiler defines it.

The version is easy to get wrong. `--clang_version=21` is accepted and means clang 0.0.21,
which lacks builtins that current libc++ relies on. A unit holding nothing but
`#include <vector>` fails with 115 errors under that spelling, the first being
`__type_pack_element is not a template`, and exports with only the prototype diagnostics
described below under 210000.

The compiler's predefined macros say which mode to use:

```bash
cc --version                 # the project's compiler, not necessarily the host's default
cc -dM -E -x c /dev/null     # the macros it predefines for C
cc -dM -E -x c++ /dev/null   # and for C++, a different set
```

With `__clang_major__`, use `--clang --clang_version=<major*10000 + minor*100 +
patchlevel>`. With `__GNUC__` and no `__clang_major__`, use `--gcc` or `--g++` with
`--gnu_version` built from `__GNUC__`, `__GNUC_MINOR__` and `__GNUC_PATCHLEVEL__`. With
`_MSC_VER`, use `--microsoft --microsoft_version=<_MSC_VER>`. Pass one mode only:
`--gcc --g++` together fails with "language modes specified are incompatible".

The standard comes next: `--c11`, `--c17`, `--c23`, `--c++11` to `--c++26`, or for MSVC's
`/std:` spellings `--ms_c++14`, `--ms_c++17`, `--ms_c++20` and `--ms_c++latest`. A GNU or
Clang dialect also chooses between ISO and GNU modes. The compiler's default is the GNU
dialect (`--no_strict_gnu`); an ISO `-std` such as `-std=c11`, as opposed to `-std=gnu11`,
corresponds to `--strict_gnu`. The difference shows: ISO mode defines `__STRICT_ANSI__`,
which hides POSIX and GNU declarations in glibc (`strdup`, `popen`, `M_PI`), and a unit read
in the wrong mode reports calls to them as implicit declarations. chen uses `--strict_gnu`
only when the unit's `-std` names the ISO language.

## Predefined macros

edga is a single binary with no EDG installation around it, and it does not read EDG's
predefined macro file. System headers test the compiler's own macros (`__APPLE__`,
`__x86_64__`, `__SIZEOF_POINTER__` and many more), so they must be supplied, as a file passed
with `--preinclude_macros`. chen's recipe has two parts.

First, remove from the compiler's dump the macros the front end must own: those whose value
follows from the front end's options (`__STDC__`, `__STDC_VERSION__`, `__STDC_HOSTED__`,
`__cplusplus`), the ones the preprocessor defines itself (`__FILE__`, `__LINE__`, `__DATE__`,
`__TIME__`, `__COUNTER__`, `__BASE_FILE__`, `__INCLUDE_LEVEL__`), and gates for extensions
the front end does not implement, so that code takes its portable path: Apple's blocks
(`__BLOCKS__`) and ARM NEON (`__ARM_NEON`, `__ARM_NEON__`, `__ARM_NEON_FP`). This is
`EdgaRunner.FrontEndOwned` in chen.

Second, pass `--diag_suppress=46`. The front end predefines more macros than that list,
among them `__CHAR16_TYPE__`, `__STDC_UTF_16__`, `__STDC_EMBED_FOUND__`, `__EXCEPTIONS` and
the C++ feature-test macros `__cpp_*`, and redefining one of them is diagnostic 46,
"is predefined; attempted redefinition ignored", which is an error and would set the
unit's status to `errors` for no fault of the project's code. With the diagnostic
suppressed, those macros keep the front end's value, which describes what the front end
implements.

```bash
cc -dM -E -x c /dev/null |
  grep -vE '^#define (__STDC__|__STDC_VERSION__|__STDC_HOSTED__|__cplusplus|__FILE__|__LINE__|__DATE__|__TIME__|__COUNTER__|__BASE_FILE__|__INCLUDE_LEVEL__|__BLOCKS__|__ARM_NEON|__ARM_NEON__|__ARM_NEON_FP) ' \
  > predefined-c.h

edga --clang --clang_version=210000 --c11 --diag_suppress=46 \
     --preinclude_macros predefined-c.h [include directories] file.c
```

For C++, collect the dump with `-x c++` into its own file and use the same filter. Without
the filter and the suppression, the raw C++ dump from Apple clang 21 produces 35 such errors
on an empty unit; with them the unit is `ok`. The remaining warnings, such as `predefined
meaning of "__clang_version__" discarded`, are harmless.

## Include directories

The front end separates project include directories from system ones, and the document uses
the difference: a file reached through a system directory is flagged `system`, which is how a
consumer tells project code from the C library and toolchain headers without matching paths.
Give the front end the split the compiler uses:

```bash
cc -E -Wp,-v -x c /dev/null 2>&1 | sed -n '/#include <...>/,/^End/p'
```

Project directories from the build (`-I`, and `-isystem` directories inside the project) go
to edga as `-I`; the compiler's own directories go as `--sys_include`, in the order the
compiler prints them. For C++ on the macOS host used here, that order is the SDK's
`usr/include/c++/v1` (libc++), the toolchain's `lib/clang/21/include`, the SDK's
`usr/include`, and the frameworks directories.

From a compilation database, the rest maps directly: the driver named in the command is the
compiler whose macros to collect, `-std` maps to the standard option, `-D` and `-U` become
`--define_macro` and `--undefine_macro`, and `-include` becomes `--preinclude`.

## Scope

`--edga-root` names the part of the tree the analysis owns: bodies are exported for
non-system files under it, and a routine declared there but defined elsewhere is listed with
`defined: false` and its declared signature, so calls to it still resolve. `--edga-headers`
also exports bodies defined in headers outside the root, for a header-only library or an
analysis of headers. chen never gives a header a unit of its own; each project header belongs
to the first unit in path order that includes it, so it is always written from the same unit
with the same settings.

## Shims chen adds

A few situations need a define beyond the compiler's macros. chen adds each of these, and a
driver by hand needs them in the same situations:

- **libc++.** libc++'s vectorised algorithms are alias templates of clang's dependent-size
  vector types, which the front end cannot instantiate, and libc++ leaves them out when
  optimising for size. chen passes `--define_macro __OPTIMIZE_SIZE__` to any C++ unit whose
  include directories contain a path ending in `c++/v1`.
- **`_Float128`.** edga configurations built without float128 support (aarch64 Linux, and
  x86_64 Linux on musl) define `_Float128` as `long double` where glibc's headers expect the
  compiler's own type.
- **C23 `va_start`.** GCC 15 and Clang 21 spell it with `__builtin_c23_va_start`, which the
  front end does not have; for C23 units chen defines it in terms of `__builtin_va_start`.

## Template prototypes

By default edga exports the bodies of templates the unit does not instantiate, as prototypes
with the template parameters in place (see the
[format reference](json-format.md#templates-and-their-instances)). To find them, edga has the
front end check every template body instead of deferring unused ones as GNU and Clang
compilers do. Some system headers written for those compilers hold template bodies that fail
that check; in current libc++, `__algorithm/simd_utils.h` and `__locale_dir/num.h` do,
even with `__OPTIMIZE_SIZE__`. The errors go to `diagnostics`, the status becomes `errors` and the exit
status 2, and the document is still complete. `--edga-no-prototypes` restores the deferral
and the clean status, without the prototype bodies. A driver that reports only `failed` units
can leave prototypes on; one that shows `errors` to its users should explain these or turn
prototypes off.

## A C++ unit from start to finish

The following exports a C++ unit that includes `<vector>` on the macOS host:

```bash
SDK=$(xcrun --show-sdk-path)
TC=$(dirname "$(dirname "$(xcrun --find clang)")")   # the toolchain's usr directory
cc -dM -E -x c++ /dev/null |
  grep -vE '^#define (__STDC__|__STDC_VERSION__|__STDC_HOSTED__|__cplusplus|__FILE__|__LINE__|__DATE__|__TIME__|__COUNTER__|__BASE_FILE__|__INCLUDE_LEVEL__|__BLOCKS__|__ARM_NEON|__ARM_NEON__|__ARM_NEON_FP) ' \
  > predefined-cxx.h

edga --clang --clang_version=210000 --no_strict_gnu --c++17 --diag_suppress=46 \
     --preinclude_macros predefined-cxx.h \
     --sys_include "$SDK/usr/include/c++/v1" \
     --sys_include "$TC/lib/clang/21/include" \
     --sys_include "$SDK/usr/include" \
     --define_macro __OPTIMIZE_SIZE__ \
     --edga-out unit.json --edga-root . unit.cpp
```

It exits with 2 and `status: errors`, the errors coming from the libc++ template bodies
named in the previous section; adding `--edga-no-prototypes` gives exit 0 and `status: ok`. Check the result
with:

```bash
python3 -c "import json; d = json.load(open('unit.json')); print(d['tu']['status'], len(d['diagnostics']))"
```

For a whole project: read the compilation database, compute the options for each unit as
above, run one edga process per unit with a time limit (chen allows ten minutes), and collect
the documents. Record a `failed` unit with its first error message, as chen's parse report
does; a test file that includes a gtest header the checkout does not vendor, for example,
fails for that reason, and the message says so.

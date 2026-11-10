# edga

edga runs the [EDG C/C++ front end](https://github.com/edgcpp/compiler) on one C or C++
translation unit and writes the program the front end built as JSON. The document keeps what
the front end resolved rather than what the text says: the type of every expression, implicit
conversions, pointer arithmetic, overload and virtual call resolution, constructors and
destructors, folded constants together with the expression they came from, object lifetimes and
layout, and the macro invocation every expanded node came from.

[chen](https://github.com/AppThreat/chen) reads the document in its `edg2atom` frontend to build a
code property graph, as an alternative to its Eclipse CDT based C/C++ frontend, and
[atom](https://github.com/AppThreat/atom) exposes the choice as `--c-frontend edg`. edga has no
dependency on either and can be used by any tool that wants a resolved C or C++ program.

## Getting edga

Each [GitHub release](https://github.com/AppThreat/edga/releases) carries a single static binary
for each platform, with a SHA-256 file next to it and a CycloneDX SBOM (`sbom-edga.cdx.json`)
for the release:

| Platform | Asset | Notes |
|---|---|---|
| Linux x86_64 | `edga-linux-amd64` | static, glibc |
| Linux ARM64 | `edga-linux-arm64` | static, glibc |
| Linux x86_64, musl | `edga-linuxmusl-amd64` | static, built in Alpine |
| Linux ARM64, musl | `edga-linuxmusl-arm64` | static, built in Alpine |
| macOS ARM64 | `edga-darwin-arm64` | macOS 13 and later |
| Windows x86_64 | `edga-windows-amd64.exe` | MSVC, static CRT |
| Windows ARM64 | `edga-windows-arm64.exe` | MSVC, static CRT |

```bash
curl -LO https://github.com/AppThreat/edga/releases/download/v0.2.0/edga-linux-amd64
curl -LO https://github.com/AppThreat/edga/releases/download/v0.2.0/edga-linux-amd64.sha256
sha256sum -c edga-linux-amd64.sha256
chmod +x edga-linux-amd64 && mv edga-linux-amd64 /usr/local/bin/edga
edga --version
```

Every push to main also publishes the same binaries to `ghcr.io/appthreat/edga:main-<platform>`,
which chen's and atom's CI pull with `oras pull` to test against what main exports.

## Usage

```bash
edga [front end options...] --edga-out tu.json --edga-root <project dir> file.c
```

| Option | Meaning |
|---|---|
| `--edga-out <file>` | Where the JSON goes. The default is standard output. |
| `--edga-root <dir>` | Function bodies are exported only for files under this directory. Functions defined elsewhere are listed by their declaration. |
| `--edga-headers` | Also export the bodies of functions defined in headers outside `--edga-root`. |
| `--edga-version`, `--version` | Print the exporter's version, the EDG commit it was built from and its configuration fingerprint. |
| `--edga-help`, `--help` | Print the options above. |

Every other option goes to the front end unchanged, for example `--c11`, `--c++17`, `--gcc`,
`--gnu_version=130300`, `--clang`, `--include_directory <dir>`, `--define_macro NAME=value`,
`--sys_include <dir>` or `--preinclude_macros <file>`. EDG documents its options in
`doc/source/ext_intf.rst` in its repository.

Without a dialect option the file name chooses the language: `.cpp`, `.cc`, `.cxx`, `.c++`, `.C`,
`.hpp`, `.hh`, `.hxx` and `.ipp` are compiled as C++ in the front end's default mode, and any
other file as C17. Pass `--c11`, `--c23`, `--c++20`, `--gcc`, `--g++` and so on to choose
explicitly.

The exit status is the front end's. It is 0 when the front end reported no errors and 2 when it
reported errors; the document is written in both cases. It is 4 when the front end stopped early,
for example at an `#error` directive or an `#include` it cannot open: the document then has
`"status": "failed"` and only the diagnostics. A source file that cannot be read gives exit status
4 and no document.

edga is a single binary. It does not read EDG's predefined macro file, so pass the macros of the
compiler being imitated with `--preinclude_macros` (or point `--edg_base_dir` at an EDG
installation), and it writes nothing next to the source files, so a read-only source tree works.
A tool that drives edga across a whole project, choosing the dialect and macros per unit from a
compilation database, is described in [how EDG and edga work](docs/how-edga-works.md#driving-the-front-end-as-the-projects-compiler).

### What comes out

```json
{
  "schema": "edga/1",
  "tool": { "name": "edga", "version": "0.2.0", "edgCommit": "3c6b73b...", "config": "linux-x86_64-..." },
  "tu": { "path": "src/main.c", "language": "c", "status": "ok", "mode": { "gcc": true, "gpp": false, "clang": false, "microsoft": false } },
  "files": [ ... ], "macros": [ ... ], "macroInvocations": [ ... ], "inlineNamespaces": [ ... ],
  "globals": [ ... ], "routines": [ ... ], "types": [ ... ], "enumerators": [ ... ],
  "diagnostics": [ ... ]
}
```

Shared things (files, macros, macro invocations, globals, routines, types) are flat tables joined
by id, and each routine's body is a nested tree of statements and expressions. A translation unit
with errors is still exported, with `"status": "errors"` and the parts the front end could build.
The [JSON format reference](docs/json-format.md) describes every table and field, and
`schema/edga-1.schema.json` is the contract the tests check every document against.

## Documentation

- [JSON format reference](docs/json-format.md): the tables, positions and macro provenance, and
  what a consumer can rely on between versions.
- [How EDG and edga work](docs/how-edga-works.md): the front end's architecture and intermediate
  language, how the exporter walks it, and how chen and atom use the result.
- [EDG and Eclipse CDT compared](docs/edg-versus-cdt.md): the two designs, their strengths, and
  what the difference means in a code property graph.
- [Maintaining edga](docs/maintaining.md): updating the EDG commit, the golden tests on each
  platform, and cutting a release.

## Building

The EDG sources are a git submodule. Its test suite is large and not needed for the build, so
`tools/fetch-edg.sh` fetches the pinned commit one commit deep without it
(`git submodule update --init` works too, and fetches everything):

```bash
tools/fetch-edg.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target edga
build/bin/edga --version
```

The build needs CMake 3.19 or later, Ninja, Python 3 (for EDG's configuration tool) and a C++14
compiler. The front end's sources are compiled unmodified. Its configuration for the host is
chosen from `config/macro-conf` (`linux-x86_64`, `linux-x86_64-musl`, `linux-aarch64`,
`macos-arm64`, `windows-x86_64`, `windows-arm64`); set `-DEDGA_MACRO_CONF=<name>` to choose one
explicitly.

For the static musl build, `tools/build-alpine.sh [build directory]` builds in an Alpine
container and runs the golden tests on the result.

On Windows, build from a Visual Studio developer environment so CMake finds MSVC (use
`vcvarsall.bat arm64` for Windows ARM64):

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target edga
```

## Tests

The golden tests export every fixture in `tests/fixtures` and compare the result with
`tests/expected`, and validate each document against the schema when the `jsonschema` Python
module is installed:

```bash
python3 tests/run.py --edga build/bin/edga            # compare with tests/expected
python3 tests/run.py --edga build/bin/edga --update   # rewrite the expected output
```

The fixtures declare what they use instead of including system headers, so the output does not
depend on the host's C library. A `// edga-options:` first line gives a fixture its front end
options. Windows targets are LLP64, so the fixtures whose types differ there have their own
expected files under `tests/expected/windows-llp64`. On Windows, run the same script with
`python tests\run.py --edga build\bin\edga.exe`.

Source changes must pass `clang-format --dry-run --Werror src/*.cpp src/*.h`, which CI checks.

## Licence

edga is licensed under the Apache License 2.0. The EDG front end is licensed under the Apache
License 2.0 with LLVM Exceptions; see `NOTICE`.

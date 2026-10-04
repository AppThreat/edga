# edga

edga runs the [EDG C/C++ front end](https://github.com/edgcpp/compiler) on one translation unit
and writes the program it built as JSON: files and includes, macros and their invocations, types,
functions with their bodies, variables, and diagnostics. The JSON keeps what the front end
resolved: the type of every expression, implicit conversions, pointer arithmetic, overloaded
operators and virtual calls, constructors and destructors, folded constants with the expression
they came from, and where macro-expanded code came from.

[chen](https://github.com/AppThreat/chen) reads it to build a code property graph, as an
alternative to its CDT-based C/C++ frontend.

## Usage

```bash
edga [front end options...] --edga-out tu.json --edga-root <project dir> file.c
```

| Option | Meaning |
|---|---|
| `--edga-out <file>` | Where the JSON goes (default: standard output). |
| `--edga-root <dir>` | Function bodies are exported only for files under this directory; functions defined elsewhere are listed by their declaration. |
| `--edga-headers` | Also export the bodies of functions defined in headers outside `--edga-root`. |
| `--edga-version`, `--version` | The exporter's version, the EDG commit it was built from and its configuration. |

Every other option goes to the front end, for example `--c11`, `--c++17`, `--gcc`,
`--gnu_version=130300`, `--clang`, `--include_directory <dir>`, `--define_macro NAME=value`,
`--sys_include <dir>`, `--edg_base_dir <dir>`. See EDG's documentation of its command-line
options (`doc/source/ext_intf.rst` in the EDG repository).

A translation unit with errors is still exported, with `"status": "errors"` and the parts the
front end could build; one the front end gave up on is written with `"status": "failed"`.

edga is a single binary: it does not read EDG's predefined macro file (pass the compiler's macros
with `--preinclude_macros`, or `--edg_base_dir` for an EDG installation), and it writes nothing
next to the source files, so a read-only source tree works.

## Releases

Each GitHub release carries edga for linux-amd64 and linux-arm64 (static, glibc), linuxmusl-amd64
and linuxmusl-arm64 (static, musl, built in Alpine: `tools/build-alpine.sh`) and darwin-arm64, with
SHA-256 files and a CycloneDX SBOM (`sbom-edga.cdx.json`, from `tools/sbom.py`). atom's container
images install it; chen finds edga with `EDGA_PATH`, `--edga-path` or on the `PATH`.

## Building

The EDG sources are a git submodule. Its test suite is large and not needed for the build, so
`tools/fetch-edg.sh` fetches the pinned commit one deep without it (`git submodule update --init`
works too):

```bash
tools/fetch-edg.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target edga
```

The build needs CMake 3.19+, Python 3 (EDG's configuration tool), and a C++14 compiler. The front
end's configuration for the host is chosen from `config/macro-conf` (`linux-x86_64`,
`linux-aarch64`, `macos-arm64`); set `-DEDGA_MACRO_CONF=<name>` to pick one. The front end's
sources are compiled unmodified.

## Tests

```bash
python3 tests/run.py --edga build/bin/edga            # compare with tests/expected
python3 tests/run.py --edga build/bin/edga --update   # rewrite the expected output
```

The fixtures declare what they use instead of including system headers, so the output does not
depend on the host's C library.

## Licence

edga is licensed under the Apache License 2.0. The EDG front end is licensed under the Apache
License v2.0 with LLVM Exceptions; see `NOTICE`.

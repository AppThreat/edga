# Maintaining edga

This page is for whoever updates the EDG front end, changes the exporter, or cuts a release.

## Updating the EDG commit

The front end is the `third_party/edgcpp` submodule, pinned to one commit of
[edgcpp/compiler](https://github.com/edgcpp/compiler). Upstream fixes land often, so the pin moves
to the head of its default branch before each release and whenever chen needs a fix:

```bash
tools/fetch-edg.sh
commit=$(git ls-remote https://github.com/edgcpp/compiler.git HEAD | cut -f1)
git -C third_party/edgcpp fetch --depth 1 --filter=blob:none origin "$commit"
git -C third_party/edgcpp checkout --detach "$commit"
git add third_party/edgcpp
```

Then build and run the golden tests. EDG's sources are compiled unmodified, and CMake reads the
front end's file list from EDG's own `src/CMakeLists.txt`, so new source files come along without
a change here. A new kind or operator in the IL appears in `src/il_names.inc`, which
`tools/gen_il_names.py` generates at build time from EDG's enumerations.

If the expected output changes, check that each difference is an upstream fix (a better resolved
type, a constant now folded, a diagnostic gone) before rewriting the goldens, and say which in the
commit message. The commit subject names the new commit, as in "EDG at 3c6b73b: upstream fixes
since c37d30e", and the body lists the upstream changes that affect the output.

## Golden tests on every platform

`tests/run.py --update` rewrites the expected files for the configuration it runs on. The
fixtures avoid system headers, so Linux, musl and macOS builds share `tests/expected`. Windows
targets are LLP64 (`size_t` is `unsigned long long`), which shifts type ids and changes
size-dependent diagnostics, so the fixtures that differ there have their own files in
`tests/expected/windows-llp64`. A change that touches the output therefore needs `--update` run
twice: once on Linux or macOS, and once on Windows (x86_64 or ARM64; they share the directory),
committing both.

A new fixture should come as a pair where it can: one input that shows the behaviour and a
twin that differs only in the detail being tested, so the test fails if the behaviour regresses.
`default_language.c` and `default_language.cpp` are an example: the same line, exported as C and
as C++.

Install the `jsonschema` Python module before running the tests, so every document is also
validated against `schema/edga-1.schema.json`. A change to the document needs the schema updated
in the same commit, along with [the format reference](json-format.md) if a consumer would care.

## Continuous integration

`.github/workflows/ci.yml` runs on every pull request and merge-group build: a `clang-format`
check (clang-format 18, as Ubuntu 24.04 installs it), the build and golden tests on Linux x86_64
and ARM64, macOS ARM64, and Windows x86_64 and ARM64, and the musl builds in Alpine. Every job is a
required status check on main, so renaming a job or a matrix entry needs the branch protection
updated with it.

Actions are pinned to commit SHAs. Refresh them with
[gh-actlock](https://github.com/esacteksab/gh-actlock) before a release, with a token so the GitHub API
does not rate-limit it:

```bash
GITHUB_TOKEN=$(gh auth token) gh actlock -u
```

## Cutting a release

1. Update the EDG commit as described above, and refresh the pinned actions.
2. Set the new version in `project(edga VERSION ...)` in `CMakeLists.txt`. The binary's
   `--version`, the document's `tool.version` and the SBOM all take it from there.
3. Merge the pull request, then tag the merge commit on main and push the tag:

   ```bash
   git tag -s v0.2.0 -m "edga 0.2.0"
   git push origin v0.2.0
   ```

4. The tag starts `.github/workflows/release.yml`. It builds edga for each platform, runs the
   golden tests on each binary, and creates the GitHub release with generated notes. It attaches
   every binary with its `.sha256` file, and the CycloneDX SBOM `sbom-edga.cdx.json` that
   `tools/sbom.py` writes, naming the EDG commit.
5. Edit the release notes to say what changed for a consumer: new fields, kinds or behaviour, and
   the EDG commit.

## Binaries from main

Each push to main also builds every platform and pushes the binaries to
`ghcr.io/appthreat/edga:main-<platform>` and `:<commit>-<platform>` with oras. chen's and atom's
CI pull `main-<platform>` through their `edga-from-main` action, and atom's CI images pull it in
`ci/Dockerfile`, so a change on edga's main reaches their tests before it is released. When those
repositories move to a released version, they should pull the release assets (or a tag pushed
for it) instead, and the push from main can be dropped.

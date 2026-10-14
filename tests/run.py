#!/usr/bin/env python3
"""Golden tests: each fixture under tests/fixtures is exported and compared with
tests/expected/<fixture>.json.

Fixtures declare what they use instead of including system headers, so the output does not
depend on the host's C library. The first line of a fixture may give front end options:
`// edga-options: --c++17`. Run with --update to rewrite the expected files.

A build whose target's data model changes what the fixtures' types are has its own expected
directory, tests/expected/<configuration>/ (windows-x86_64: LLP64, where size_t is an unsigned
long long, the ids of the types after it shift, and size-dependent code draws diagnostics of
its own). Those files are per-configuration; the others are shared by every build.
"""

import argparse
import difflib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCHEMA = HERE.parent / "schema" / "edga-1.schema.json"
FIXTURES = HERE / "fixtures"
EXPECTED = HERE / "expected"
OPTIONS_PREFIX = "// edga-options:"

# Configurations with an expected directory of their own: LLP64 targets, where size_t is an
# unsigned long long, the ids of the types after it shift, and size-dependent code draws
# diagnostics of its own (the Windows x86_64 and ARM64 targets model the same types, so they
# share the directory). --update writes it.
OWN_EXPECTED_DIRECTORIES = {
    "windows-x86_64": "windows-llp64",
    "windows-arm64": "windows-llp64",
}


def fixture_options(path):
    first = path.read_text(encoding="utf-8").splitlines()[:1]
    if first and first[0].startswith(OPTIONS_PREFIX):
        return first[0][len(OPTIONS_PREFIX):].split()
    return ["--c11"] if path.suffix == ".c" else ["--c++17"]


def configuration_of(edga):
    """The edga build's configuration name (its --edga-version output), e.g. windows-x86_64."""
    version = subprocess.run([str(edga), "--edga-version"], capture_output=True, text=True)
    for line in version.stdout.splitlines():
        if line.startswith("configuration "):
            return line.split()[1]
    return ""


def expected_dir(configuration):
    """Where a build's expected files are: its own directory when it has one, the shared one
    otherwise."""
    name = OWN_EXPECTED_DIRECTORIES.get(configuration)
    if name is not None:
        return EXPECTED / name
    return EXPECTED


def expected_path(directory, fixture):
    """A fixture's expected file: the build's own directory has the fixtures its target's data
    model makes differ; the rest use the shared one."""
    own = directory / (fixture + ".json")
    return own if own.exists() else EXPECTED / (fixture + ".json")


def normalise(document):
    """Drops what differs between builds and checkouts: the tool's version and configuration,
    and the directory the fixtures are in."""
    document.pop("tool", None)
    for f in document.get("files", []):
        f["path"] = os.path.basename(f["path"])
        if "written" in f:
            f["written"] = os.path.basename(f["written"])
    tu = document.get("tu", {})
    if "path" in tu:
        tu["path"] = os.path.basename(tu["path"])
    for d in document.get("diagnostics", []):
        if "file" in d:
            d["file"] = os.path.basename(d["file"])
    return document


def export(edga, fixture):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "out.json"
        cmd = [
            str(edga),
            # plain char is unsigned on aarch64 Linux and signed on the other hosts
            "--signed_chars",
            *fixture_options(fixture),
            "--edga-root",
            str(FIXTURES),
            "--edga-out",
            str(out),
            str(fixture),
        ]
        run = subprocess.run(cmd, capture_output=True, text=True, cwd=FIXTURES)
        if not out.exists():
            raise RuntimeError(f"{fixture.name}: no output\n{run.stderr}")
        return json.loads(out.read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--edga", required=True, help="the edga binary")
    parser.add_argument("--update", action="store_true", help="rewrite the expected files")
    parser.add_argument("names", nargs="*", help="fixtures to run (default: all)")
    args = parser.parse_args()

    try:
        import jsonschema

        validator = jsonschema.Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8")))
    except ImportError:
        validator = None
        print("jsonschema is not installed: the output is not checked against the schema")

    fixtures = sorted(p for p in FIXTURES.iterdir() if p.suffix in (".c", ".cpp"))
    if args.names:
        fixtures = [p for p in fixtures if p.stem in args.names or p.name in args.names]
    directory = expected_dir(configuration_of(Path(args.edga).resolve()))
    print(f"expected files: {directory.name}")
    failures = 0
    for fixture in fixtures:
        # absolute: edga runs from the fixtures directory
        document = export(Path(args.edga).resolve(), fixture)
        if validator is not None:
            errors = sorted(validator.iter_errors(document), key=lambda e: list(e.path))
            if errors:
                failures += 1
                print(f"FAIL {fixture.name}: does not match the schema")
                for e in errors[:5]:
                    print(f"     {list(e.path)}: {e.message[:200]}")
                continue
        actual = json.dumps(normalise(document), indent=1, sort_keys=False) + "\n"
        shared_path = EXPECTED / (fixture.name + ".json")
        if args.update:
            # a build with its own directory writes there only the fixtures its target makes
            # differ; what matches the shared files stays shared
            own_path = directory / (fixture.name + ".json")
            shared = shared_path.read_text(encoding="utf-8") if shared_path.exists() else ""
            if directory != EXPECTED and actual != shared:
                own_path.parent.mkdir(parents=True, exist_ok=True)
                own_path.write_text(actual, encoding="utf-8", newline="\n")
                print(f"updated {own_path.relative_to(EXPECTED)}")
            else:
                own_path.unlink(missing_ok=True)
                shared_path.write_text(actual, encoding="utf-8", newline="\n")
                print(f"updated {shared_path.name}")
            continue
        this_expected = expected_path(directory, fixture.name)
        expected = this_expected.read_text(encoding="utf-8") if this_expected.exists() else ""
        if actual != expected:
            failures += 1
            print(f"FAIL {fixture.name}")
            sys.stdout.writelines(
                difflib.unified_diff(
                    expected.splitlines(True),
                    actual.splitlines(True),
                    f"expected/{this_expected.parent.name}",
                    "actual",
                    n=2,
                )
            )
        else:
            print(f"ok   {fixture.name}")
    if failures:
        print(f"{failures} of {len(fixtures)} fixtures differ")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

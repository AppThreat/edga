#!/usr/bin/env python3
"""Golden tests: each fixture under tests/fixtures is exported and compared with
tests/expected/<fixture>.json.

Fixtures declare what they use instead of including system headers, so the output does not
depend on the host's C library. The first line of a fixture may give front end options:
`// edga-options: --c++17`. Run with --update to rewrite the expected files.
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


def fixture_options(path):
    first = path.read_text(encoding="utf-8").splitlines()[:1]
    if first and first[0].startswith(OPTIONS_PREFIX):
        return first[0][len(OPTIONS_PREFIX):].split()
    return ["--c11"] if path.suffix == ".c" else ["--c++17"]


def normalise(document):
    """Drops what differs between builds and checkouts: the tool's version and configuration,
    and the directory the fixtures are in."""
    document.pop("tool", None)
    for f in document.get("files", []):
        f["path"] = os.path.basename(f["path"])
    tu = document.get("tu", {})
    if "path" in tu:
        tu["path"] = os.path.basename(tu["path"])
    return document


def export(edga, fixture):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "out.json"
        cmd = [
            str(edga),
            "--clear_flag=use_predefined_macro_file",
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
    failures = 0
    for fixture in fixtures:
        document = export(args.edga, fixture)
        if validator is not None:
            errors = sorted(validator.iter_errors(document), key=lambda e: list(e.path))
            if errors:
                failures += 1
                print(f"FAIL {fixture.name}: does not match the schema")
                for e in errors[:5]:
                    print(f"     {list(e.path)}: {e.message[:200]}")
                continue
        actual = json.dumps(normalise(document), indent=1, sort_keys=False) + "\n"
        expected_path = EXPECTED / (fixture.name + ".json")
        if args.update:
            expected_path.write_text(actual, encoding="utf-8")
            print(f"updated {expected_path.name}")
            continue
        expected = expected_path.read_text(encoding="utf-8") if expected_path.exists() else ""
        if actual != expected:
            failures += 1
            print(f"FAIL {fixture.name}")
            sys.stdout.writelines(
                difflib.unified_diff(
                    expected.splitlines(True),
                    actual.splitlines(True),
                    "expected",
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

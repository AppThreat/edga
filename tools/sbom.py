#!/usr/bin/env python3
"""Writes the CycloneDX SBOM of an edga build: edga and the EDG front end it is built from, at the
commit the submodule pins.

    tools/sbom.py sbom-edga.cdx.json
"""

import datetime
import json
import re
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def edga_version():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(edga VERSION ([0-9.]+)", cmake)
    if not match:
        raise SystemExit("sbom: no project version in CMakeLists.txt")
    return match.group(1)


def edg_commit():
    tree = subprocess.run(
        ["git", "ls-tree", "HEAD", "third_party/edgcpp"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.split()
    if len(tree) < 3 or tree[1] != "commit":
        raise SystemExit("sbom: third_party/edgcpp is not a submodule of this checkout")
    return tree[2]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    version = edga_version()
    commit = edg_commit()
    edga_ref = f"pkg:github/appthreat/edga@{version}"
    edg_ref = f"pkg:github/edgcpp/compiler@{commit}"
    sbom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{uuid.uuid4()}",
        "version": 1,
        "metadata": {
            "timestamp": datetime.datetime.now(datetime.timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ"
            ),
            "component": {
                "type": "application",
                "name": "edga",
                "version": version,
                "description": "Runs the EDG C/C++ front end on a translation unit and writes "
                "the program it built as JSON.",
                "purl": edga_ref,
                "bom-ref": edga_ref,
                "licenses": [{"license": {"id": "Apache-2.0"}}],
                "externalReferences": [
                    {"type": "vcs", "url": "https://github.com/AppThreat/edga"}
                ],
            },
        },
        "components": [
            {
                "type": "library",
                "group": "edgcpp",
                "name": "compiler",
                "version": commit,
                "description": "The EDG C/C++ front end, compiled into edga unmodified.",
                "purl": edg_ref,
                "bom-ref": edg_ref,
                "licenses": [{"expression": "Apache-2.0 WITH LLVM-exception"}],
                "externalReferences": [
                    {"type": "vcs", "url": "https://github.com/edgcpp/compiler"}
                ],
            }
        ],
        "dependencies": [
            {"ref": edga_ref, "dependsOn": [edg_ref]},
            {"ref": edg_ref, "dependsOn": []},
        ],
    }
    Path(sys.argv[1]).write_text(json.dumps(sbom, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

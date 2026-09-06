#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


_SHA40 = re.compile(r"^[0-9a-f]{40}$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_reference(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not key or not value:
            raise ValueError(f"Invalid reference-toolchain entry: {raw_line!r}")
        values[key] = value

    required = {
        "CXX26_REPOSITORY",
        "CXX26_BRANCH",
        "CXX26_SNAPSHOT",
        "CXX26_REVISION",
        "CXX26_ASSET",
    }
    missing = required - values.keys()
    if missing:
        raise ValueError(f"Missing reference-toolchain fields: {sorted(missing)}")
    return values


def require_sha(name: str, value: str) -> None:
    if not _SHA40.fullmatch(value):
        raise ValueError(f"{name} must be a 40-character lowercase Git SHA")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-version", required=True)
    parser.add_argument("--release-version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-repository", required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--miracle-revision", required=True)
    parser.add_argument("--switch-revision", required=True)
    parser.add_argument("--vcpkg-revision", required=True)
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument("--reference-file", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    for name, value in (
        ("source revision", args.source_revision),
        ("Miracle revision", args.miracle_revision),
        ("Switch revision", args.switch_revision),
        ("vcpkg revision", args.vcpkg_revision),
    ):
        require_sha(name, value)

    reference = read_reference(args.reference_file)
    require_sha("reference-toolchain revision", reference["CXX26_REVISION"])

    artifact = args.artifact.resolve()
    if not artifact.is_file():
        raise FileNotFoundError(artifact)

    checksum_file = artifact.with_name(artifact.name + ".sha256")
    if not checksum_file.is_file():
        raise FileNotFoundError(checksum_file)

    metadata = {
        "schemaVersion": 1,
        "project": {
            "name": "Nyx",
            "version": args.release_version,
            "baseVersion": args.project_version,
            "tag": args.tag,
            "license": "LGPL-3.0-only",
        },
        "source": {
            "repository": args.source_repository,
            "branch": "master",
            "revision": args.source_revision,
        },
        "platform": {
            "os": "linux",
            "architecture": "x86_64",
        },
        "referenceToolchain": {
            "repository": reference["CXX26_REPOSITORY"],
            "branch": reference["CXX26_BRANCH"],
            "snapshot": reference["CXX26_SNAPSHOT"],
            "revision": reference["CXX26_REVISION"],
            "asset": reference["CXX26_ASSET"],
        },
        "dependencies": {
            "Miracle": {
                "repository": "spwn02/Miracle",
                "revision": args.miracle_revision,
            },
            "Switch": {
                "repository": "spwn02/Switch",
                "revision": args.switch_revision,
            },
            "vcpkg": {
                "repository": "microsoft/vcpkg",
                "revision": args.vcpkg_revision,
            },
        },
        "artifacts": [
            {
                "name": artifact.name,
                "sha256": sha256(artifact),
                "checksumFile": checksum_file.name,
            }
        ],
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()

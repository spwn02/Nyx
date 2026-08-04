#!/usr/bin/env python

import argparse
import json
import glob
from pathlib import Path
import os
from typing import Optional, Self
from dataclasses import dataclass
import logging


VERSION = 1
LOG_LEVEL = os.environ.get("LOG_LEVEL", "DEBUG").upper()
logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(filename)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger(__file__)

type Serialized = dict[str, Optional[str] | float]


@dataclass
class Shader:
    name: str
    path: str
    last_modified: float
    compiled: Optional[str] = None

    def compile(self) -> Optional[Self]:
        match self.name.split(".")[-1]:
            case "wgsl":
                logger.info("WGSL")
            case "glsl":
                return logging.error(
                    "GLSL is currently unsupported, skipping %s", self.name
                )
            case _:
                return logging.error(
                    "Unrecognized file extension, skipping %s", self.name
                )

        return self

    def to_dict(self) -> Serialized:
        return {
            "path": self.path,
            "last_modified": self.last_modified,
            "compiled": self.compiled,
        }


class Manifest:
    def __init__(self, dir: Path, output: Path) -> None:
        self.json: dict[str, int | Serialized] = {}
        self.dir: Path = dir
        self.output: Path = output

        if output.exists():
            with open(output, "r") as file:
                self.json = json.loads(file.read())

                [
                    self.json.pop(n)
                    for n in [
                        n
                        for n, s in self.json.items()
                        if not isinstance(s, int)
                        and isinstance(s["path"], str)
                        and not os.path.exists(s["path"])
                    ]
                ]

        else:
            self.json["version"] = VERSION

        # TODO: Process shaders in parallel across multiple threads
        for file in glob.glob(f"{dir}/*"):
            path = Path(file)
            if path == output:
                continue

            name = file.split("/")[-1]
            last_modified = path.stat().st_mtime
            if entry := self.json.get(name):
                if isinstance(entry, int):
                    raise RuntimeError("Expected Shader object, got int")

                # TODO: Validate whether compiled path exists
                if last_modified != entry["last_modified"]:
                    if shader := Shader(name, file, last_modified).compile():
                        logger.info("Updated: %s", name)
                        self.json[name] = shader.to_dict()
                else:
                    logger.debug("Shader is up to date, skipped: %s", name)

            else:
                if shader := Shader(name, file, last_modified).compile():
                    logger.info("Added: %s", name)
                    self.json[name] = shader.to_dict()

    def save(self) -> None:
        with open(self.output, "w") as file:
            file.write(json.dumps(self.json, indent=2, sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        prog="NyxShaders",
        description="Compiles the shaders to SPIR-V if they are not up to date, emits manifest file on success.",
    )

    parser.add_argument(
        "--dir", type=str, required=True, help="The target shader assets directory path"
    )
    parser.add_argument(
        "--output",
        type=str,
        required=True,
        help="Path where the generated manifest file should be saved",
    )

    args = parser.parse_args()

    print(f"Validating assets inside: {args.dir}")
    print(f"Writing output manifest file to: {args.output}")

    manifest = Manifest(Path(args.dir), Path(args.output))
    manifest.save()

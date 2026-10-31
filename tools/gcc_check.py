#!/usr/bin/env python3
"""Syntax-check the project's sources with GCC, using the dev preset's compile commands.

Catches GCC-only warnings (such as -Wshadow on members) without a Linux machine. On macOS, JUCE's
own headers do not compile with GCC, so only diagnostics in the project's files are reported.
"""

from __future__ import annotations

import argparse
import json
import platform
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROJECT_DIRS = ("modules", "tests", "demo")
DROPPED_FLAGS = {"-c", "-DDEBUG=1", "-D_DEBUG=1"}
DROPPED_WITH_VALUE = {"-o", "-arch"}


def gcc_arguments(command: str, compiler: str, sysroot: str | None) -> list[str]:
    args = shlex.split(command)[1:]
    result = [compiler, "-fsyntax-only", "-DNDEBUG=1"]
    if sysroot:
        result += ["-isysroot", sysroot]
    skip = False
    for arg in args:
        if skip:
            skip = False
        elif arg in DROPPED_WITH_VALUE:
            skip = True
        elif arg not in DROPPED_FLAGS:
            result.append(arg)
    return result


def project_diagnostics(output: str, root: Path) -> list[str]:
    prefixes = tuple(f"{root / directory}/" for directory in PROJECT_DIRS)
    return sorted({line for line in output.splitlines() if line.startswith(prefixes) and (": error:" in line or ": warning:" in line)})


def project_entries(commands: list[dict[str, str]]) -> list[dict[str, str]]:
    return [entry for entry in commands if "/_deps/" not in entry["file"] and entry["file"].endswith(".cpp") and "/Debug/" in entry.get("output", "")]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++-14", help="The GCC driver to use")
    parser.add_argument("--build-dir", type=Path, default=PROJECT_ROOT / "build" / "dev", help="A configured build with compile_commands.json")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    commands = json.loads((args.build_dir / "compile_commands.json").read_text())
    sysroot = None
    if platform.system() == "Darwin":
        sysroot = subprocess.run(["xcrun", "--show-sdk-path"], check=True, capture_output=True, text=True).stdout.strip()

    def check(entry: dict[str, str]) -> list[str]:
        result = subprocess.run(gcc_arguments(entry["command"], args.compiler, sysroot), cwd=entry["directory"], capture_output=True, text=True)
        return project_diagnostics(result.stderr, PROJECT_ROOT)

    entries = project_entries(commands)
    with ThreadPoolExecutor() as pool:
        diagnostics = sorted({line for lines in pool.map(check, entries) for line in lines})
    for line in diagnostics:
        print(line)
    print(f"{len(entries)} files checked, {len(diagnostics)} diagnostics")
    return 1 if diagnostics else 0


if __name__ == "__main__":
    sys.exit(main())

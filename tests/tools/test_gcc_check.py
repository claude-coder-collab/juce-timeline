import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import gcc_check


def test_arguments_drop_output_architecture_and_debug_defines() -> None:
    command = "/usr/bin/c++ -DDEBUG=1 -DX=1 -I/inc -arch arm64 -std=c++26 -o out.o -c /src/a.cpp"
    assert gcc_check.gcc_arguments(command, "g++-14", "/sdk") == [
        "g++-14",
        "-fsyntax-only",
        "-DNDEBUG=1",
        "-isysroot",
        "/sdk",
        "-DX=1",
        "-I/inc",
        "-std=c++26",
        "/src/a.cpp",
    ]
    assert "-isysroot" not in gcc_check.gcc_arguments(command, "g++", None)


def test_only_project_diagnostics_are_reported() -> None:
    root = Path("/repo")
    output = "\n".join(
        [
            "/repo/modules/a.cpp:1:2: warning: shadows [-Wshadow]",
            "/repo/modules/a.cpp:1:2: warning: shadows [-Wshadow]",
            "/repo/build/_deps/juce-src/x.h:3:4: error: juce",
            "/repo/tests/b.cpp:5:6: error: broken",
            "/repo/demo/c.cpp:7:8: note: context",
        ]
    )
    assert gcc_check.project_diagnostics(output, root) == [
        "/repo/modules/a.cpp:1:2: warning: shadows [-Wshadow]",
        "/repo/tests/b.cpp:5:6: error: broken",
    ]


def test_entries_skip_dependencies_and_other_configurations() -> None:
    entries = [
        {"file": "/repo/modules/a.cpp", "output": "x/Debug/a.o"},
        {"file": "/repo/modules/a.cpp", "output": "x/Release/a.o"},
        {"file": "/repo/build/_deps/juce/b.cpp", "output": "x/Debug/b.o"},
        {"file": "/repo/modules/c.mm", "output": "x/Debug/c.o"},
    ]
    assert gcc_check.project_entries(entries) == [entries[0]]

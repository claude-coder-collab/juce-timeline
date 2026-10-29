import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check


def test_steps_cover_presets_and_only_test_tested_presets() -> None:
    names = [name for name, _ in check.steps(["dev", "tidy"], ["a.cpp"], "python", fix_format=False)]
    assert names == [
        "pytest",
        "clang-format",
        "configure dev",
        "build dev",
        "test dev",
        "configure tidy",
        "build tidy",
    ]


def test_format_step_checks_or_fixes() -> None:
    check_step = dict(check.steps([], ["a.cpp", "b.h"], "python", fix_format=False))["clang-format"]
    fix_step = dict(check.steps([], ["a.cpp"], "python", fix_format=True))["clang-format"]
    assert check_step == ["clang-format", "--dry-run", "--Werror", "a.cpp", "b.h"]
    assert fix_step == ["clang-format", "-i", "a.cpp"]


def test_source_files_lists_tracked_cpp_files() -> None:
    files = check.source_files(check.PROJECT_ROOT)
    assert "modules/jtimeline/core/jtimeline_TimeMapping.cpp" in files
    assert all(name.endswith((".cpp", ".h")) for name in files)


def test_parse_args_defaults() -> None:
    args = check.parse_args([])
    assert (args.preset, args.fix_format, args.keep_going, args.keep_builds) == (None, False, False, False)


def test_sanitizer_builds_are_removed_unless_kept() -> None:
    names = [name for name, _ in check.steps(["asan"], [], "python", fix_format=False)]
    assert names[-1] == "clean asan"
    kept = [name for name, _ in check.steps(["asan"], [], "python", fix_format=False, keep_builds=True)]
    assert "clean asan" not in kept

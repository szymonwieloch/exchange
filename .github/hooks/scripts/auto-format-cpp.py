#!/usr/bin/env python3
"""
PostToolUse hook: auto-format modified .h and .cpp files with clang-format.
"""
import json
import subprocess
import sys
import os
from pathlib import Path


# Tools that modify files
EDIT_TOOLS = {
    "replace_string_in_file",
    "create_file",
    "multi_replace_string_in_file",
    "vscode_renameSymbol",
}

# Extensions to auto-format
FORMAT_EXTENSIONS = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".c"}


def find_clang_format():
    """Find the clang-format binary, preferring clang-format-18."""
    for candidate in ["clang-format-18", "clang-format"]:
        path = subprocess.run(
            ["which", candidate], capture_output=True, text=True
        ).stdout.strip()
        if path:
            return candidate
    return None


def extract_file_paths(tool_input: dict) -> list[str]:
    """Extract file paths from tool input, handling various structures."""
    paths = []

    # Single filePath
    if "filePath" in tool_input:
        paths.append(tool_input["filePath"])

    # Multi-replace: replacements array with filePath per entry
    if "replacements" in tool_input:
        for entry in tool_input["replacements"]:
            if "filePath" in entry:
                paths.append(entry["filePath"])

    return paths


def should_format(file_path: str) -> bool:
    """Check if file has a C/C++ extension."""
    ext = Path(file_path).suffix.lower()
    return ext in FORMAT_EXTENSIONS


def format_files(file_paths: list[str], clang_format: str) -> None:
    """Run clang-format on the given files."""
    existing = [p for p in set(file_paths) if os.path.isfile(p)]
    if not existing:
        return

    result = subprocess.run(
        [clang_format, "-i"] + existing,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"clang-format error: {result.stderr}", file=sys.stderr)


def main():
    try:
        input_data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, EOFError):
        # No valid input — nothing to do
        sys.exit(0)

    tool_name = input_data.get("tool_name", "")
    if tool_name not in EDIT_TOOLS:
        sys.exit(0)

    tool_input = input_data.get("tool_input", {})
    file_paths = extract_file_paths(tool_input)

    cpp_files = [p for p in file_paths if should_format(p)]
    if not cpp_files:
        sys.exit(0)

    clang_format = find_clang_format()
    if not clang_format:
        print("clang-format not found, skipping auto-format", file=sys.stderr)
        sys.exit(0)

    format_files(cpp_files, clang_format)

    # Output: continue (no blocking)
    print(json.dumps({"continue": True}))


if __name__ == "__main__":
    main()

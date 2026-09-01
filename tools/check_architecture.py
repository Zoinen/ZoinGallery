#!/usr/bin/env python3
"""Small, dependency-free source architecture guard.

File limits are deterministic CI gates. Function findings are intentionally
reported as warnings by default: the parser is conservative and exists to
point reviewers at the next extraction boundary, not to replace a C++ or QML
parser. A justified exception can be annotated with
``architecture-check: allow-file-lines <reason>`` or
``architecture-check: allow-function-lines <reason>``.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


EXCLUDED_DIRS = {
    ".git",
    ".github",
    "__pycache__",
    "cmakefiles",
    "dds_image",
    "exiv2",
    "node_modules",
    "tests",
    "tinyexif",
    "vendor",
}
FILE_EXEMPTION = "architecture-check: allow-file-lines"
FUNCTION_EXEMPTION = "architecture-check: allow-function-lines"


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    message: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--qml-root", action="append", default=[])
    parser.add_argument("--cpp-root", action="append", default=[])
    parser.add_argument("--qml-limit", type=int, default=800)
    parser.add_argument("--main-qml-limit", type=int, default=500)
    parser.add_argument("--cpp-limit", type=int, default=1500)
    parser.add_argument("--function-limit", type=int, default=80)
    parser.add_argument("--require-bound-qml", action="store_true")
    parser.add_argument("--fail-on-file-limit", action="store_true")
    parser.add_argument("--fail-on-function-limit", action="store_true")
    return parser.parse_args()


def is_excluded(path: Path, scan_root: Path) -> bool:
    try:
        parts = path.relative_to(scan_root).parts[:-1]
    except ValueError:
        parts = path.parts[:-1]
    for part in parts:
        lowered = part.lower()
        if lowered in EXCLUDED_DIRS or lowered.startswith("build"):
            return True
    return False


def source_files(roots: Iterable[Path], suffix: str) -> list[Path]:
    result: set[Path] = set()
    for root in roots:
        if root.is_file() and root.suffix.lower() == suffix:
            result.add(root.resolve())
            continue
        if not root.exists():
            continue
        for path in root.rglob(f"*{suffix}"):
            if path.is_file() and not is_excluded(path, root):
                result.add(path.resolve())
    return sorted(result)


def line_count(text: str) -> int:
    return len(text.splitlines())


def has_nearby_exemption(lines: list[str], line: int, marker: str) -> bool:
    first = max(0, line - 4)
    return any(marker in value for value in lines[first:line])


def matching_brace(text: str, opening: int) -> int | None:
    depth = 0
    quote = ""
    escaped = False
    line_comment = False
    block_comment = False
    index = opening
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if line_comment:
            if char == "\n":
                line_comment = False
            index += 1
            continue
        if block_comment:
            if char == "*" and next_char == "/":
                block_comment = False
                index += 2
            else:
                index += 1
            continue
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            index += 1
            continue
        if char == "/" and next_char == "/":
            line_comment = True
            index += 2
            continue
        if char == "/" and next_char == "*":
            block_comment = True
            index += 2
            continue
        if char in {'"', "'", "`"}:
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return None


def logical_lines(text: str) -> int:
    return sum(
        1 for line in text.splitlines()
        if line.strip() and not line.lstrip().startswith("//")
    )


def function_findings(path: Path, text: str, limit: int) -> list[Finding]:
    lines = text.splitlines()
    patterns = (
        re.compile(r"\bfunction\s+([A-Za-z_$][\w$]*)\s*\([^;{}]*\)\s*\{"),
        re.compile(
            r"(?m)^[^#\n;{}]*?(?:\n[^#\n;{}]*){0,5}"
            r"\b([A-Za-z_]\w*(?:::\w+)+)\s*\([^;{}]*\)"
            r"\s*(?:const\s*)?(?:noexcept\s*)?(?:->[^{}]+)?\{"),
    )
    findings: list[Finding] = []
    occupied_until = -1
    for pattern in patterns:
        for match in pattern.finditer(text):
            opening = text.find("{", match.start(), match.end())
            if opening < 0 or opening <= occupied_until:
                continue
            closing = matching_brace(text, opening)
            if closing is None:
                continue
            occupied_until = closing
            start_line = text.count("\n", 0, match.start()) + 1
            count = logical_lines(text[opening + 1:closing])
            if count <= limit or has_nearby_exemption(
                    lines, start_line, FUNCTION_EXEMPTION):
                continue
            name = match.group(1)
            findings.append(Finding(
                path, start_line,
                f"function {name} has about {count} logical lines "
                f"(guideline {limit})"))
    return findings


def inspect_file(path: Path, args: argparse.Namespace) -> tuple[list[Finding], list[Finding]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    file_findings: list[Finding] = []
    if (args.require_bound_qml and path.suffix.lower() == ".qml"
            and "pragma ComponentBehavior: Bound" not in text):
        file_findings.append(Finding(
            path, 1, "missing pragma ComponentBehavior: Bound"))
    if FILE_EXEMPTION not in "\n".join(lines[:30]):
        if path.suffix.lower() == ".qml":
            limit = (args.main_qml_limit if path.name == "main.qml"
                     else args.qml_limit)
        else:
            limit = args.cpp_limit
        count = line_count(text)
        if count > limit:
            file_findings.append(Finding(
                path, 1, f"file has {count} lines (limit {limit})"))
    return file_findings, function_findings(path, text, args.function_limit)


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    qml_roots = [root / value for value in (args.qml_root or ["qml"])]
    cpp_roots = [root / value for value in (args.cpp_root or ["."])]
    files = source_files(qml_roots, ".qml") + source_files(cpp_roots, ".cpp")
    file_findings: list[Finding] = []
    function_warnings: list[Finding] = []
    for path in files:
        file_result, function_result = inspect_file(path, args)
        file_findings.extend(file_result)
        function_warnings.extend(function_result)

    for finding in file_findings:
        print(f"ARCHITECTURE: {finding.path}:{finding.line}: {finding.message}")
    for finding in function_warnings:
        print(f"ARCHITECTURE-WARNING: {finding.path}:{finding.line}: "
              f"{finding.message}")
    print(f"Architecture check: {len(files)} files, "
          f"{len(file_findings)} file violations, "
          f"{len(function_warnings)} function warnings")
    if args.fail_on_file_limit and file_findings:
        return 1
    if args.fail_on_function_limit and function_warnings:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

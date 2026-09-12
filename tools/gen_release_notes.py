#!/usr/bin/env python3
"""Generate GitHub Release notes from CHANGELOG.md + git commits."""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import subprocess
import sys


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True, encoding="utf-8", errors="replace").strip()


def changelog_section(text: str, keys: list[str]) -> str:
    keys = [k for k in keys if k]
    if not keys:
        return ""
    pattern = re.compile(
        r"^##\s*\[?(" + "|".join(re.escape(k) for k in keys) + r")\]?[^\n]*\n(.*?)(?=^##\s|\Z)",
        re.M | re.S,
    )
    m = pattern.search(text)
    return m.group(2).strip() if m else ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("-o", "--output", default="release_notes.md")
    args = ap.parse_args()

    tag, ver = args.tag, args.version
    lines = [
        f"## DiskOrganizer {tag}",
        "",
        "Windows x64 安装包（共享 Qt 运行时，含 `windeployqt` 依赖）。",
        "",
        "> 本地开发默认可构建 **/MT 全静态**单文件；CI Release 使用共享 Qt 以便在 GitHub-hosted runner 打包。",
        "",
    ]

    changelog = pathlib.Path("CHANGELOG.md")
    if changelog.exists():
        section = changelog_section(
            changelog.read_text(encoding="utf-8"),
            [ver, f"v{ver}", tag, tag.lstrip("v")],
        )
        if section:
            lines += ["### 更新说明", "", section, ""]

    prev = ""
    try:
        prev = subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0", f"{tag}^"],
            text=True,
            encoding="utf-8",
            errors="replace",
            stderr=subprocess.DEVNULL,
        ).strip()
    except subprocess.CalledProcessError:
        prev = ""

    lines += ["### Commits", ""]
    if prev:
        lines.append(f"自 `{prev}` 以来：")
        lines.append("")
        try:
            commits = git("log", "--pretty=format:- %s (%h)", f"{prev}..{tag}")
        except subprocess.CalledProcessError:
            commits = ""
    else:
        try:
            commits = git("log", "--pretty=format:- %s (%h)", "-30")
        except subprocess.CalledProcessError:
            commits = ""
    if commits:
        lines.append(commits)
    lines.append("")

    out = pathlib.Path(args.output)
    body = "\n".join(lines)
    out.write_text(body, encoding="utf-8")
    # Windows CI 控制台常为 cp1252，直接 print 中文会 UnicodeEncodeError
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
    except Exception:
        pass
    try:
        print(body)
    except UnicodeEncodeError:
        sys.stdout.buffer.write(body.encode("utf-8", errors="replace"))
        sys.stdout.buffer.write(b"\n")
    print(f"Wrote {out.resolve()} ({len(body)} chars)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Safely add or remove cosmic-cycle-app shortcuts in COSMIC's RON map."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import stat
import tempfile

BEGIN_MARKER = "// cosmic-cycle-app: begin"
END_MARKER = "// cosmic-cycle-app: end"
MARKED_BLOCK = re.compile(
    rf"(?ms)^[ \t]*{re.escape(BEGIN_MARKER)}\n"
    rf".*?^[ \t]*{re.escape(END_MARKER)}[ \t]*\n?"
)
LEGACY_BINDING = re.compile(
    r"""(?msx)
    ^[ \t]*
    \(
      \s*modifiers:\s*\[
        (?:Alt(?:\s*,\s*Shift)?|Shift\s*,\s*Alt)
      \]\s*,?
      \s*key:\s*"grave"\s*,?
    \)
    \s*:\s*
    Spawn\("
      [^"\n]*cosmic-cycle-app-windows(?:\s+--reverse)?
    "\)\s*,?[ \t]*\n?
    """
)
GRAVE_BINDING = re.compile(
    r"""(?sx)
    \(
      \s*modifiers:\s*\[(?P<modifiers>[^\]]*)\]\s*,?
      \s*key:\s*"grave"\s*,?
    \)
    \s*:
    """
)


def read_config(path: Path) -> tuple[str, int]:
    if not path.exists():
        return "{}\n", 0o600
    if not path.is_file():
        raise ValueError(f"shortcut path is not a regular file: {path}")
    return path.read_text(encoding="utf-8"), stat.S_IMODE(path.stat().st_mode)


def validate_ron_map(content: str) -> None:
    stripped = content.strip()
    if not stripped.startswith("{") or not stripped.endswith("}"):
        raise ValueError("shortcut config is not a RON map; refusing to edit it")


def remove_managed_block(content: str) -> str:
    return MARKED_BLOCK.sub("", content)


def remove_legacy_bindings(content: str) -> str:
    return LEGACY_BINDING.sub("", content)


def reject_conflicts(content: str) -> None:
    for match in GRAVE_BINDING.finditer(content):
        modifiers = {
            item.strip() for item in match.group("modifiers").split(",")
            if item.strip()
        }
        if modifiers in ({"Alt"}, {"Alt", "Shift"}):
            rendered = ", ".join(sorted(modifiers))
            raise ValueError(
                f"Alt+grave shortcut conflict ({rendered}); "
                "remove or reassign that binding first"
            )


def managed_block(binary: Path) -> str:
    command = json.dumps(str(binary), ensure_ascii=False)
    reverse_command = json.dumps(f"{binary} --reverse", ensure_ascii=False)
    return (
        f"    {BEGIN_MARKER}\n"
        "    (\n"
        "        modifiers: [Alt],\n"
        '        key: "grave",\n'
        f"    ): Spawn({command}),\n"
        "    (\n"
        "        modifiers: [Alt, Shift],\n"
        '        key: "grave",\n'
        f"    ): Spawn({reverse_command}),\n"
        f"    {END_MARKER}\n"
    )


def install_content(content: str, binary: Path) -> str:
    validate_ron_map(content)
    updated = remove_managed_block(content)
    updated = remove_legacy_bindings(updated)
    reject_conflicts(updated)
    closing_brace = updated.rfind("}")
    prefix = updated[:closing_brace].rstrip()
    suffix = updated[closing_brace:]
    return f"{prefix}\n{managed_block(binary)}{suffix.lstrip()}"


def remove_content(content: str) -> str:
    validate_ron_map(content)
    return remove_managed_block(content)


def atomic_write(path: Path, content: str, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent, prefix=f".{path.name}.", suffix=".tmp"
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary_path, mode)
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("install", "remove"))
    parser.add_argument("config", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--delete-if-empty", action="store_true")
    args = parser.parse_args()

    content, mode = read_config(args.config)
    if args.action == "install":
        if args.binary is None or not args.binary.is_absolute():
            parser.error("install requires an absolute --binary path")
        updated = install_content(content, args.binary)
    else:
        updated = remove_content(content)

    if args.check:
        return 0
    if args.delete_if_empty and re.fullmatch(r"\s*\{\s*\}\s*", updated):
        args.config.unlink(missing_ok=True)
    elif updated != content or not args.config.exists():
        atomic_write(args.config, updated, mode)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError) as error:
        raise SystemExit(f"cosmic-cycle-app: {error}") from error

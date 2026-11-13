#!/usr/bin/env python3
"""Copy secrets from .env into sdkconfig.

ESP-IDF has no notion of runtime environment variables: everything the
firmware knows is compiled in from Kconfig, which is stored in sdkconfig.
This script bridges the two, so the only file holding real credentials is
the gitignored .env.

Usage:
    cp .env.example .env    # once, then edit .env
    python3 scripts/apply_env.py
    idf.py build

Options:
    --dry-run    show what would change without writing sdkconfig
    --env PATH   read a different .env file
"""

from __future__ import annotations

import argparse
import logging
import re
import sys
from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Key in .env -> Kconfig symbol in sdkconfig
MAPPING: dict[str, str] = {
    "WIFI_SSID": "CONFIG_WIFI_SSID",
    "WIFI_PASSWORD": "CONFIG_WIFI_PASSWORD",
    "MQTT_BROKER_IP": "CONFIG_MQTT_BROKER_IP",
    "MQTT_USER": "CONFIG_MQTT_USER",
    "MQTT_PASSWORD": "CONFIG_MQTT_PASSWORD",
}

SECRET_HINTS: tuple[str, ...] = ("PASSWORD", "SECRET", "TOKEN", "KEY")

log = logging.getLogger("apply_env")


class ChangeKind(Enum):
    UPDATED = auto()
    ADDED = auto()


@dataclass(frozen=True, slots=True)
class Change:
    kind: ChangeKind
    env_key: str
    symbol: str
    value: str

    def masked_value(self) -> str:
        if any(hint in self.env_key.upper() for hint in SECRET_HINTS):
            return "<empty>" if not self.value else f"<{len(self.value)} chars>"
        return self.value or "<empty>"

    def __str__(self) -> str:
        verb = "updated" if self.kind is ChangeKind.UPDATED else "added"
        return f"{verb:<7}  {self.symbol:<22} = {self.masked_value()}"


def parse_env(path: Path) -> dict[str, str]:
    """Read KEY=VALUE lines, ignoring blanks, comments and 'export '."""
    values: dict[str, str] = {}
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.strip().removeprefix("export ").lstrip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            log.warning("%s:%d: no '=' found, skipping", path.name, lineno)
            continue

        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


def quote(value: str) -> str:
    """Render a Kconfig string value the way sdkconfig stores it."""
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def apply_updates(sdkconfig_lines: list[str], env: dict[str, str]) -> tuple[list[str], list[Change]]:
    """Return updated sdkconfig lines plus the list of changes that were made."""
    lines = list(sdkconfig_lines)
    changes: list[Change] = []

    for env_key, symbol in MAPPING.items():
        if env_key not in env:
            continue

        value = env[env_key]
        new_line = f"{symbol}={quote(value)}\n"
        pattern = re.compile(rf"^{re.escape(symbol)}=.*$")

        for i, line in enumerate(lines):
            if pattern.match(line.rstrip("\n")):
                if line != new_line:
                    lines[i] = new_line
                    changes.append(Change(ChangeKind.UPDATED, env_key, symbol, value))
                break
        else:
            lines.append(new_line)
            changes.append(Change(ChangeKind.ADDED, env_key, symbol, value))

    return lines, changes


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="show changes without writing sdkconfig"
    )
    parser.add_argument(
        "--env",
        type=Path,
        default=ROOT / ".env",
        help="path to the .env file (default: .env in project root)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    args = parse_args(argv)

    env_path: Path = args.env
    sdkconfig_path = ROOT / "sdkconfig"

    if not env_path.is_file():
        log.error("%s not found.\n       cp .env.example .env   then fill in your values", env_path)
        return 1

    if not sdkconfig_path.is_file():
        log.error(
            "%s not found.\n       Run 'idf.py build' once so ESP-IDF generates it, then re-run this.",
            sdkconfig_path,
        )
        return 1

    env = parse_env(env_path)
    if unknown := sorted(set(env) - set(MAPPING)):
        log.warning("ignoring unknown keys in %s: %s", env_path.name, ", ".join(unknown))

    sdkconfig_lines = sdkconfig_path.read_text().splitlines(keepends=True)
    new_lines, changes = apply_updates(sdkconfig_lines, env)

    if not changes:
        print(f"sdkconfig already matches {env_path.name}, nothing to do.")
        return 0

    for change in changes:
        print(change)

    if args.dry_run:
        print("\n--dry-run: sdkconfig was not modified.")
        return 0

    sdkconfig_path.write_text("".join(new_lines))
    print(f"\nWrote {sdkconfig_path.name}. Rebuild with: idf.py build")
    return 0


if __name__ == "__main__":
    sys.exit(main())

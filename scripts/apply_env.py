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

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Key in .env -> Kconfig symbol in sdkconfig
MAPPING = {
    "WIFI_SSID": "CONFIG_WIFI_SSID",
    "WIFI_PASSWORD": "CONFIG_WIFI_PASSWORD",
    "MQTT_BROKER_IP": "CONFIG_MQTT_BROKER_IP",
    "MQTT_USER": "CONFIG_MQTT_USER",
    "MQTT_PASSWORD": "CONFIG_MQTT_PASSWORD",
}

SECRET_HINTS = ("PASSWORD", "SECRET", "TOKEN", "KEY")


def parse_env(path: Path) -> dict:
    """Read KEY=VALUE lines, ignoring blanks, comments and 'export '."""
    values = {}
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        if "=" not in line:
            print(f"warning: {path.name}:{lineno}: no '=' found, skipping", file=sys.stderr)
            continue
        key, _, val = line.partition("=")
        key = key.strip()
        val = val.strip()
        if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
            val = val[1:-1]
        values[key] = val
    return values


def quote(value: str) -> str:
    """Render a Kconfig string value the way sdkconfig stores it."""
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def mask(key: str, value: str) -> str:
    if any(hint in key.upper() for hint in SECRET_HINTS):
        return "<empty>" if value == "" else f"<{len(value)} chars>"
    return value or "<empty>"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true",
                        help="show changes without writing sdkconfig")
    parser.add_argument("--env", default=str(ROOT / ".env"),
                        help="path to the .env file (default: .env in project root)")
    args = parser.parse_args()

    env_path = Path(args.env)
    sdkconfig_path = ROOT / "sdkconfig"

    if not env_path.is_file():
        print(f"error: {env_path} not found.\n"
              f"       cp .env.example .env   then fill in your values", file=sys.stderr)
        return 1

    if not sdkconfig_path.is_file():
        print(f"error: {sdkconfig_path} not found.\n"
              f"       Run 'idf.py build' once so ESP-IDF generates it, then re-run this.",
              file=sys.stderr)
        return 1

    env = parse_env(env_path)
    unknown = sorted(set(env) - set(MAPPING))
    if unknown:
        print(f"warning: ignoring unknown keys in {env_path.name}: {', '.join(unknown)}",
              file=sys.stderr)

    text = sdkconfig_path.read_text()
    lines = text.splitlines(keepends=True)
    changed, added = [], []

    for key, symbol in MAPPING.items():
        if key not in env:
            continue
        new_line = f"{symbol}={quote(env[key])}\n"
        pattern = re.compile(rf"^{re.escape(symbol)}=.*$")

        replaced = False
        for i, line in enumerate(lines):
            if pattern.match(line.rstrip("\n")):
                if line.rstrip("\n") != new_line.rstrip("\n"):
                    lines[i] = new_line
                    changed.append((key, symbol, env[key]))
                replaced = True
                break
        if not replaced:
            lines.append(new_line)
            added.append((key, symbol, env[key]))

    if not changed and not added:
        print(f"sdkconfig already matches {env_path.name}, nothing to do.")
        return 0

    for key, symbol, value in changed:
        print(f"updated  {symbol:<22} = {mask(key, value)}")
    for key, symbol, value in added:
        print(f"added    {symbol:<22} = {mask(key, value)}")

    if args.dry_run:
        print("\n--dry-run: sdkconfig was not modified.")
        return 0

    sdkconfig_path.write_text("".join(lines))
    print(f"\nWrote {sdkconfig_path.name}. Rebuild with: idf.py build")
    return 0


if __name__ == "__main__":
    sys.exit(main())

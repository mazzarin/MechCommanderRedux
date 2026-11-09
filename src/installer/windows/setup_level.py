"""Port-only (the personal build): the setup's level (SPEC-install-and-updates.md's "The program's update on Windows").

A copy whose setup was at a release's level takes that release's program by the game's own swap (the files renamed
aside and replaced while it runs); any other copy runs the release's setup. So the level rises whenever the setup's
work changes: the files' names, the registry, shortcuts, links, the uninstaller. setup-level.txt, beside the setup's
script, holds the level and the SHA-256 of the script (MechCommanderRedux.iss, its line endings as LF) at that level,
and the Windows workflow checks it: a changed script fails the build until someone says whether the change changes
the setup's work.

  setup_level.py --check   prints the level; fails when the script changed since setup-level.txt was written
  setup_level.py --raise   the change changes the setup's work: the level rises, with the script's hash
  setup_level.py --keep    it doesn't (a comment, a message's wording): the same level, with the script's hash
"""

import argparse
import hashlib
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / "MechCommanderRedux.iss"
RECORD = HERE / "setup-level.txt"
LEVEL_KEY = "level"
HASH_KEY = "sha256"
LINE = re.compile(r"^([a-z0-9]+)=(\S+)$")
HASH = re.compile(r"^[0-9a-f]{64}$")


def script_hash() -> str:
    """The script's SHA-256 with its line endings as LF, so a checkout's CRLF doesn't change it."""
    return hashlib.sha256(SCRIPT.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def read_record() -> tuple[int, str]:
    """The level and hash setup-level.txt holds."""
    values = {}

    for number, line in enumerate(RECORD.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue

        match = LINE.match(line.strip())

        if not match or match.group(1) not in (LEVEL_KEY, HASH_KEY) or match.group(1) in values:
            sys.exit(f"{RECORD}: line {number} isn't one '{LEVEL_KEY}=<n>' or '{HASH_KEY}=<hex>': {line!r}")

        values[match.group(1)] = match.group(2)

    level, digest = values.get(LEVEL_KEY), values.get(HASH_KEY)

    if level is None or not level.isdigit() or int(level) < 1 or digest is None or not HASH.match(digest):
        sys.exit(f"{RECORD} needs '{LEVEL_KEY}=<a number from 1>' and '{HASH_KEY}=<64 hex digits>'")

    return int(level), digest


def write_record(level: int) -> None:
    RECORD.write_text(f"{LEVEL_KEY}={level}\n{HASH_KEY}={script_hash()}\n", encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true", help="print the level; fail when the script changed")
    action.add_argument("--raise", dest="rise", action="store_true", help="the setup's work changed: raise the level")
    action.add_argument("--keep", action="store_true", help="the setup's work didn't change: keep the level")
    args = parser.parse_args()
    level, digest = read_record()

    if args.check:
        if digest != script_hash():
            sys.exit(
                f"{SCRIPT.name} changed since {RECORD.name} was written (level {level}). If the change changes the "
                f"setup's work (the files' names, the registry, shortcuts, links, the uninstaller), run "
                f"'{pathlib.Path(__file__).name} --raise'; if not, '--keep'. Then commit {RECORD.name}."
            )

        print(level)
    elif args.rise:
        write_record(level + 1)
        print(f"Level {level + 1}")
    else:
        write_record(level)
        print(f"Level {level}, the script's new hash kept")


if __name__ == "__main__":
    main()

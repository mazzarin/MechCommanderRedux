r"""Lists every source file of the port's projects explicitly, and writes each project's .vcxproj.filters with one
filter per folder on disk, so Solution Explorer shows the real tree (abl\, ai\, ...).

Visual Studio doesn't support wildcards in .vcxproj items (it warns on load), so the projects list each file. Run this
after adding, moving or deleting a .cpp/.h: it rewrites the project's source ItemGroup (the one holding ClCompile /
ClInclude items) from the files on disk. A file's own settings (PrecompiledHeader and the like, and comments inside its
item) are kept from the current project. Filter GUIDs come from the folder's path, so reruns don't churn.

Usage:
    python scripts/ci/sync_projects.py          # update every project and its .filters
    python scripts/ci/sync_projects.py --check  # report projects that are out of date; exit 1 if any
"""
import argparse
import os
import re
import sys
import uuid

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(REPO, "src")

COMPILE_EXTENSIONS = (".cpp", ".c")
INCLUDE_EXTENSIONS = (".h", ".hpp", ".inl")
SKIP_DIRS = {"build", "__pycache__", ".vs"}

# Each project: its .vcxproj (relative to src), and which files it owns, relative to the project's folder:
# "tree" = every source file below the folder; a list = these files plus every source file below these subfolders.
PROJECTS = [
    ("lib/MCCore/MCCore.vcxproj", "tree"),
    ("apps/MCRedux/MCRedux.vcxproj", ["Main.cpp", "MCConsole.cpp", "MCConsole.h", "MCCrashTrace.cpp", "MCCrashTrace.h",
                                      "MCGameFolder.cpp", "MCGameFolder.h", "stdafx.cpp", "stdafx.h"]),
    ("apps/MCRedux/mc_tests.vcxproj", ["MCConsole.cpp", "MCConsole.h", "MCCrashTrace.cpp", "MCCrashTrace.h",
                                       "MCGameFolder.cpp", "MCGameFolder.h", "stdafx.cpp", "stdafx.h", "tests/"]),
    ("apps/MCEditor/MCEditor.vcxproj", "tree"),
]

ITEM = re.compile(r'[ \t]*<(ClCompile|ClInclude) Include="([^"]+)"\s*(?:/>|>(.*?)</\1>)[ \t]*\r?\n?', re.S)
ITEM_GROUP = re.compile(r"[ \t]*<ItemGroup>\s*\r?\n(?:(?!</ItemGroup>).)*?<Cl(?:Compile|Include) .*?</ItemGroup>", re.S)
FILTERS_NAMESPACE = uuid.UUID("6f7d2a52-8f0e-4c1c-9a57-3c5e0b0d6a11")


def source_kind(name):
    """ClCompile, ClInclude, or None for files the project doesn't list."""
    extension = os.path.splitext(name)[1].lower()

    if extension in COMPILE_EXTENSIONS:
        return "ClCompile"

    if extension in INCLUDE_EXTENSIONS:
        return "ClInclude"

    return None


def walk_sources(root, folder):
    """Every source file below root/folder, as Windows-style paths relative to root."""
    found = []

    for directory, subdirs, files in os.walk(os.path.join(root, folder)):
        subdirs[:] = [d for d in subdirs if d not in SKIP_DIRS]

        for name in files:
            if source_kind(name) is not None:
                found.append(os.path.relpath(os.path.join(directory, name), root).replace("/", "\\"))

    return found


def project_files(root, owns):
    """The files a project owns, sorted the way Solution Explorer sorts them (case-insensitive)."""
    if owns == "tree":
        files = walk_sources(root, ".")
    else:
        files = []

        for entry in owns:
            if entry.endswith("/"):
                files.extend(walk_sources(root, entry))
            elif os.path.isfile(os.path.join(root, entry)):
                files.append(entry.replace("/", "\\"))

    return sorted(set(files), key=lambda path: path.lower())


def render_items(files, existing, indent, newline):
    """The new source ItemGroup: ClCompile items, then ClInclude items, each keeping its current settings."""
    lines = [f"{indent}<ItemGroup>"]

    for kind in ("ClCompile", "ClInclude"):
        for path in files:
            if source_kind(path) != kind:
                continue

            body = existing.get(path.lower())

            if body is not None and body.strip():
                lines.append(f'{indent}  <{kind} Include="{path}">{body.rstrip()}{newline}{indent}  </{kind}>')
            else:
                lines.append(f'{indent}  <{kind} Include="{path}" />')

    lines.append(f"{indent}</ItemGroup>")
    return newline.join(lines)


def render_filters(files, newline):
    """The .vcxproj.filters: a filter per folder (and its parents), and each file in its folder's filter."""
    folders = set()

    for path in files:
        parts = path.split("\\")[:-1]

        for depth in range(1, len(parts) + 1):
            folders.add("\\".join(parts[:depth]))

    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">']

    if folders:
        lines.append("  <ItemGroup>")

        for folder in sorted(folders, key=str.lower):
            guid = uuid.uuid5(FILTERS_NAMESPACE, folder.lower())
            lines.append(f'    <Filter Include="{folder}">')
            lines.append(f"      <UniqueIdentifier>{{{guid}}}</UniqueIdentifier>")
            lines.append("    </Filter>")

        lines.append("  </ItemGroup>")

    for kind in ("ClCompile", "ClInclude"):
        items = [path for path in files if source_kind(path) == kind]

        if not items:
            continue

        lines.append("  <ItemGroup>")

        for path in items:
            folder = path.rpartition("\\")[0]

            if folder:
                lines.append(f'    <{kind} Include="{path}">')
                lines.append(f"      <Filter>{folder}</Filter>")
                lines.append(f"    </{kind}>")
            else:
                lines.append(f'    <{kind} Include="{path}" />')

        lines.append("  </ItemGroup>")

    lines.append("</Project>")
    return newline.join(lines) + newline


def read_text(path):
    """The file's text (BOM stripped), whether it had a BOM, and its newline."""
    with open(path, "rb") as file:
        data = file.read()

    bom = data.startswith(b"\xef\xbb\xbf")
    text = data[3 if bom else 0:].decode("utf-8")
    return text, bom, "\r\n" if "\r\n" in text else "\n"


def write_text(path, text, bom):
    with open(path, "wb") as file:
        file.write((b"\xef\xbb\xbf" if bom else b"") + text.encode("utf-8"))


def sync(project, owns, check):
    """Updates one project and its filters; returns the names of the files that were (or would be) changed."""
    vcxproj = os.path.join(SRC, project)
    root = os.path.dirname(vcxproj)
    text, bom, newline = read_text(vcxproj)
    group = ITEM_GROUP.search(text)

    if group is None:
        sys.exit(f"{project}: no ItemGroup with ClCompile/ClInclude items")

    existing = {}

    for match in ITEM.finditer(group.group(0)):
        if "*" not in match.group(2):
            existing[match.group(2).lower()] = match.group(3)

    files = project_files(root, owns)
    indent = re.match(r"[ \t]*", group.group(0)).group(0)
    updated = text[:group.start()] + render_items(files, existing, indent, newline) + text[group.end():]
    changed = []

    if updated != text:
        changed.append(project)

        if not check:
            write_text(vcxproj, updated, bom)

    filters_path = vcxproj + ".filters"
    filters = render_filters(files, newline)
    current = read_text(filters_path)[0] if os.path.isfile(filters_path) else None

    if filters != current:
        changed.append(project + ".filters")

        if not check:
            write_text(filters_path, filters, True)

    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="report out-of-date projects; change nothing")
    args = parser.parse_args()
    changed = []

    for project, owns in PROJECTS:
        changed.extend(sync(project, owns, args.check))

    for name in changed:
        print(("out of date: " if args.check else "updated: ") + name)

    if not changed:
        print("projects are up to date")

    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())

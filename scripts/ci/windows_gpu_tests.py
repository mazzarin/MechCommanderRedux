#!/usr/bin/env python3
"""Port-only: the Windows GPU workflow's test shards (.github/workflows/windows-gpu.yml).

run     Runs one shard's share of the suite: every n-th test that mc_tests --list names, from the shard's index. The
        share's names are the runner's filters (a test runs when its name holds a filter), so a test runs in one shard
        only while no test's name holds another's; the report checks that. Its output goes to the console and the
        shard's log, beside the full list and the share.
report  Reads the shards' logs together: every listed test run once, the failures, the validation layer's messages by
        kind with the tests that showed them, the slowest tests and each shard's time, as Markdown for the run's
        summary. Exits with 1 on a test failed, missing or run twice, or a validation error.
canary  Makes two Vulkan calls wrong on purpose in a child process, as the suite's processes make theirs, and fails
        unless the validation layer's error comes out and reads as one here: without it, a suite with no messages
        could be a layer that wasn't loaded or printed nowhere.
"""

import argparse
import collections
import re
import subprocess
import sys
import time
from pathlib import Path

ALL_FILE = "all.txt"
SHARE_FILE = "share.txt"
LOG_FILE = "tests.log"
# Windows' longest command line (CreateProcess), less room for the program's path and options.
COMMAND_LINE_MOST = 32_000
SLOWEST_SHOWN = 15
TESTS_NAMED_MOST = 3

RESULT = re.compile(r"^\[ (  OK|FAIL) \] (.+) \((\d+) ms\)\s*$")
HEADER = re.compile(r"^\[ (?:PROC|RUN ) \] (.+?)\s*$")
FAILED = re.compile(r"^  FAILED: (.+?)\s*$")
SUMMARY = re.compile(r"^(\d+) of (\d+) tests passed, (\d+) checks passed, (\d+) failed \((\d+) ms\)")
VALIDATION = re.compile(r"Validation (Error|Warning|Performance Warning): \[ ?([^\]]+?) ?\]")
ERROR_KIND = "Error"

# The canary's Vulkan: structure types, and the version it asks for.
STRUCTURE_TYPE_APPLICATION_INFO = 0
STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1
API_VERSION_1_0 = 1 << 22
VK_SUCCESS = 0
CANARY_SECONDS = 60


def run(args: argparse.Namespace) -> int:
    program = Path(args.program).resolve()
    listing = subprocess.run([str(program), "--list"], check=True, capture_output=True, text=True, encoding="utf-8")
    names = [name for name in listing.stdout.splitlines() if name]

    if not names:
        print(f"{program} --list named no tests", file=sys.stderr)
        return 1

    share = names[args.shard :: args.shards]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / ALL_FILE).write_text("\n".join(names) + "\n", encoding="utf-8")
    (out / SHARE_FILE).write_text("\n".join(share) + "\n", encoding="utf-8")
    command = [str(program), "--game", str(Path(args.game).resolve()), "--jobs", str(args.jobs), *share]
    length = len(subprocess.list2cmdline(command))

    if length > COMMAND_LINE_MOST:
        print(f"Shard {args.shard + 1}'s {len(share)} names make a command line of {length} characters, past "
              f"{COMMAND_LINE_MOST}: run more shards.", file=sys.stderr)
        return 1

    print(f"Shard {args.shard + 1} of {args.shards}: {len(share)} of {len(names)} tests, {args.jobs} at once",
          flush=True)
    start = time.monotonic()

    with open(out / LOG_FILE, "wb") as log, subprocess.Popen(command, stdout=subprocess.PIPE,
                                                             stderr=subprocess.STDOUT) as process:
        assert process.stdout is not None  # Popen's stdout=PIPE
        for line in process.stdout:
            log.write(line)
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()

        code = process.wait()

    print(f"Shard {args.shard + 1}: exit code {code} after {time.monotonic() - start:.0f} s", flush=True)
    return code


class Shard:
    """What one shard's log says."""

    def __init__(self, folder: Path):
        self.Name = folder.name
        self.All = read_names(folder / ALL_FILE)
        self.Share = read_names(folder / SHARE_FILE)
        self.Results: dict[str, tuple[bool, int]] = {}
        self.Failed: set[str] = set()
        # The run's own summary (its last): tests passed, tests, checks passed, checks failed, ms.
        self.Summary: tuple[int, ...] | None = None
        # Validation messages by (kind, message ID): how many, and the tests they came in.
        self.Messages: dict[tuple[str, str], list] = collections.defaultdict(lambda: [0, set()])
        log = folder / LOG_FILE

        if not log.exists():
            return

        current = "(before any test)"

        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if match := HEADER.match(line):
                current = match.group(1)
            elif match := RESULT.match(line):
                self.Results[match.group(2)] = (match.group(1) == "  OK", int(match.group(3)))
            elif match := FAILED.match(line):
                self.Failed.add(match.group(1))
            elif match := SUMMARY.match(line):
                self.Summary = tuple(int(number) for number in match.groups())

            for match in VALIDATION.finditer(line):
                entry = self.Messages[(match.group(1), match.group(2))]
                entry[0] += 1
                entry[1].add(current)


def read_names(path: Path) -> list[str]:
    return [name for name in path.read_text(encoding="utf-8").splitlines() if name] if path.exists() else []


def minutes(ms: int) -> str:
    seconds = round(ms / 1000)
    return f"{seconds // 60}:{seconds % 60:02d}"


def report(args: argparse.Namespace) -> int:
    folders = sorted((path for path in Path(args.results).iterdir() if path.is_dir()), key=lambda path: path.name)
    shards = [Shard(folder) for folder in folders]
    lines = [f"## {args.title}", ""]
    problems = []

    if not shards:
        problems.append(f"No shard's results in {args.results}.")

    listed = shards[0].All if shards else []

    if any(shard.All != listed for shard in shards):
        problems.append("The shards' mc_tests --list differ.")

    ran = collections.Counter()
    failed = set()

    for shard in shards:
        ran.update(set(shard.Results) | shard.Failed)
        failed |= shard.Failed | {name for name, (passed, _) in shard.Results.items() if not passed}

    missing = [name for name in listed if name not in ran]
    twice = sorted(name for name, count in ran.items() if count > 1)
    lines += ["| Shard | Tests | Passed | Checks | Failed checks | Time |", "|---|---:|---:|---:|---:|---:|"]

    for shard in shards:
        if shard.Summary:
            passed, total, checks, failures, ms = shard.Summary
            lines.append(f"| {shard.Name} | {total} | {passed} | {checks:,} | {failures} | {minutes(ms)} |")
        else:
            lines.append(f"| {shard.Name} | {len(shard.Share)} | (no summary: stopped?) | | | |")
            problems.append(f"{shard.Name} has no summary line.")

    summaries = [shard.Summary for shard in shards if shard.Summary]
    longest = max((summary[4] for summary in summaries), default=0)
    lines += ["", f"**{sum(s[0] for s in summaries)} of {len(listed)} tests passed, "
                  f"{sum(s[2] for s in summaries):,} checks; the longest shard {minutes(longest)}.**", ""]

    messages: dict[tuple[str, str], list] = collections.defaultdict(lambda: [0, set()])

    for shard in shards:
        for key, (count, tests) in shard.Messages.items():
            messages[key][0] += count
            messages[key][1] |= tests

    errors = sum(count for (kind, _), (count, _) in messages.items() if kind == ERROR_KIND)
    lines += ["### Validation", ""]

    if messages:
        lines += ["| Kind | Message | Count | Tests |", "|---|---|---:|---|"]

        for (kind, message), (count, tests) in sorted(messages.items(), key=lambda item: -item[1][0]):
            named = "; ".join(sorted(tests)[:TESTS_NAMED_MOST])
            more = f" and {len(tests) - TESTS_NAMED_MOST} more" if len(tests) > TESTS_NAMED_MOST else ""
            lines.append(f"| {kind} | `{message}` | {count} | {named}{more} |")
    else:
        lines.append("No messages from the validation layer.")

    if errors:
        problems.append(f"{errors} validation errors.")

    for heading, names in (("Failed", sorted(failed)), ("Not run", missing), ("Run in two shards", twice)):
        if names:
            lines += ["", f"### {heading}", ""] + [f"- {name}" for name in names]
            problems.append(f"{len(names)} tests {heading.lower()}.")

    timed = sorted(((ms, name) for shard in shards for name, (_, ms) in shard.Results.items()), reverse=True)
    lines += ["", "### Slowest tests", "", "| Time | Test |", "|---:|---|"]
    lines += [f"| {ms / 1000:.1f} s | {name} |" for ms, name in timed[:SLOWEST_SHOWN]]

    if problems:
        lines += ["", "### Problems", ""] + [f"- {problem}" for problem in problems]

    text = "\n".join(lines) + "\n"
    print(text)

    if args.markdown:
        Path(args.markdown).write_text(text, encoding="utf-8")

    return 1 if problems else 0


def canary(args: argparse.Namespace) -> int:
    command = [sys.executable, "-I", str(Path(__file__).resolve()), "canary-child", "--loader", args.loader]
    child = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=CANARY_SECONDS)
    output = child.stdout + child.stderr
    print(output, end="")
    errors = [match.group(2) for match in VALIDATION.finditer(output) if match.group(1) == ERROR_KIND]

    if not errors:
        print(f"The validation layer reported nothing on calls made wrong on purpose (the child's exit code "
              f"{child.returncode}): a suite without messages would mean nothing.", file=sys.stderr)
        return 1

    print(f"The validation layer's canary: {', '.join(errors)}")
    return 0


def canary_child(args: argparse.Namespace) -> int:
    """Two calls the validation layer refuses: the application's info with the wrong structure type, and a physical
    device's properties asked for with nowhere to put them (which the layer stops before the driver sees it)."""
    import ctypes

    vulkan = ctypes.CDLL(str(Path(args.loader).resolve()))

    class ApplicationInfo(ctypes.Structure):
        _fields_ = [("sType", ctypes.c_int32), ("pNext", ctypes.c_void_p), ("pApplicationName", ctypes.c_char_p),
                    ("applicationVersion", ctypes.c_uint32), ("pEngineName", ctypes.c_char_p),
                    ("engineVersion", ctypes.c_uint32), ("apiVersion", ctypes.c_uint32)]

    class InstanceCreateInfo(ctypes.Structure):
        _fields_ = [("sType", ctypes.c_int32), ("pNext", ctypes.c_void_p), ("flags", ctypes.c_uint32),
                    ("pApplicationInfo", ctypes.POINTER(ApplicationInfo)), ("enabledLayerCount", ctypes.c_uint32),
                    ("ppEnabledLayerNames", ctypes.c_void_p), ("enabledExtensionCount", ctypes.c_uint32),
                    ("ppEnabledExtensionNames", ctypes.c_void_p)]

    def create(application_type: int) -> ctypes.c_void_p | None:
        application = ApplicationInfo(sType=application_type, pApplicationName=b"canary", apiVersion=API_VERSION_1_0)
        info = InstanceCreateInfo(sType=STRUCTURE_TYPE_INSTANCE_CREATE_INFO, pApplicationInfo=ctypes.pointer(application))
        instance = ctypes.c_void_p()
        result = vulkan.vkCreateInstance(ctypes.byref(info), None, ctypes.byref(instance))
        print(f"vkCreateInstance (application info's type {application_type}): {result}", flush=True)
        return instance if result == VK_SUCCESS else None

    # The wrong structure type: the layer reports it, the loader doesn't look.
    wrong = create(STRUCTURE_TYPE_INSTANCE_CREATE_INFO)

    if wrong is not None:
        vulkan.vkDestroyInstance(wrong, None)

    instance = create(STRUCTURE_TYPE_APPLICATION_INFO)

    if instance is None:
        return 1

    count = ctypes.c_uint32(0)
    vulkan.vkEnumeratePhysicalDevices(instance, ctypes.byref(count), None)
    devices = (ctypes.c_void_p * count.value)()
    vulkan.vkEnumeratePhysicalDevices(instance, ctypes.byref(count), devices)

    if count.value > 0:
        vulkan.vkGetPhysicalDeviceProperties(ctypes.c_void_p(devices[0]), None)

    vulkan.vkDestroyInstance(instance, None)
    sys.stdout.flush()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    run_parser = commands.add_parser("run", help="run one shard's share of the suite")
    run_parser.add_argument("--program", required=True, help="mc_tests.exe")
    run_parser.add_argument("--game", required=True, help="the game's folder")
    run_parser.add_argument("--shard", required=True, type=int, help="this shard's index, from 0")
    run_parser.add_argument("--shards", required=True, type=int, help="how many shards")
    run_parser.add_argument("--jobs", required=True, type=int, help="isolated tests run at once")
    run_parser.add_argument("--out", required=True, help="the folder for the log and lists")
    report_parser = commands.add_parser("report", help="read the shards' results together")
    report_parser.add_argument("results", help="a folder holding a folder of results per shard")
    report_parser.add_argument("--title", required=True, help="the report's heading")
    report_parser.add_argument("--markdown", help="a file for the report as well")

    for name, help_text in (("canary", "check that the validation layer's errors reach the log"),
                            ("canary-child", "the canary's wrong calls (run by canary)")):
        canary_parser = commands.add_parser(name, help=help_text)
        canary_parser.add_argument("--loader", required=True, help="vulkan-1.dll")

    args = parser.parse_args()

    if args.command == "run" and not 0 <= args.shard < args.shards:
        parser.error(f"--shard {args.shard} isn't one of {args.shards} shards")

    commands_run = {"run": run, "report": report, "canary": canary, "canary-child": canary_child}
    return commands_run[args.command](args)


if __name__ == "__main__":
    sys.exit(main())

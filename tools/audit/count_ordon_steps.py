"""Measure native Link movement entries in a stock Windows Ordon trace."""
import argparse
from collections import Counter
import math
from pathlib import Path
import re

STAGE = re.compile(r"^\[game\] stageName=(\w+)")
LOADED = re.compile(r"^\[savestate\] Loaded slot \d+ \((\w+)\)")
SWAPS = re.compile(r"^\[gx2\] frame (\d+), (\S+) swaps/s, swap interval (\d+)")
MOVE = re.compile(r"^\[watch\] mainThread 0207151C lr=02086350 r3=([0-9A-F]{8})")
PROCEDURE = re.compile(r"^\[watch\] +ld32\(r3\+305C\) = ([0-9A-F]{4})[0-9A-F]{4}")


def measure(log, start):
    stage = None
    active = False
    actor = None
    pending = False
    procedures = Counter()
    for line in log:
        if match := STAGE.match(line) or LOADED.match(line):
            stage = match[1]
            if active:
                raise ValueError("Stage reloaded or changed during the measurement window")
        if match := SWAPS.match(line):
            frame, swaps_per_second, interval = int(match[1]), float(match[2]), int(match[3])
            if active:
                if frame != start + 300 or interval != 2 or not math.isfinite(swaps_per_second):
                    raise ValueError("Expected the next 300-frame stock pacing report")
                if pending or procedures.total() != 300:
                    raise ValueError(f"Expected 300 Link entries, observed {procedures.total()}")
                return procedures, swaps_per_second
            if frame == start:
                if stage != "F_SP103" or interval != 2:
                    raise ValueError("The window must start in Ordon Village with stock swap interval 2")
                active = True
        if not active:
            continue
        if pending:
            match = PROCEDURE.match(line)
            if not match:
                raise ValueError("Missing Link procedure field. Set WWHD_WATCH_EXPR=3+305c.")
            procedures[match[1]] += 1
            pending = False
        if match := MOVE.match(line):
            if actor is not None and actor != match[1]:
                raise ValueError("Link's guest address changed during the measurement window")
            actor = match[1]
            pending = True
    raise ValueError(f"Missing complete pacing window at frames {start} through {start + 300}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Trace from WWHD_WATCH=0207151C and WWHD_WATCH_EXPR=3+305c")
    parser.add_argument("--start-frame", type=int, default=301)
    args = parser.parse_args()
    try:
        with args.log.open(encoding="utf-8", errors="replace") as log:
            procedures, measured_rate = measure(log, args.start_frame)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{exc}\n")

    print(f"Ordon Village, 300 stock frames from {args.start_frame}: {procedures.total()} native Link movement entries")
    print("Link procedures " + ", ".join(f"0x{proc}: {count}" for proc, count in sorted(procedures.items())))
    print(f"Measured {measured_rate:.1f} movement entries per wall second in this window")


if __name__ == "__main__":
    main()

"""Compare stock and experimental WWHD_ORDON_TRACE captures from Ordon Village."""

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import median

MICROSECONDS = 1_000_000
EVENTS = ("legacy", "input", "move", "collision", "camera", "draw", "pose", "skeleton",
          "orbit", "viewangle", "clip", "picture", "present")
CHANNELS = ("move", "camera", "pose")


@dataclass
class Event:
    kind: str
    us: int
    phase: str
    ticket: int
    counters: dict[str, tuple[int, int]]
    phases: dict[str, str]


def parse(lines, channels=CHANNELS):
    events = []
    for number, line in enumerate(lines, 1):
        _, marker, fields = line.partition("[ordon60] ")
        if not marker:
            continue
        try:
            data = dict(field.split("=", 1) for field in fields.split())
            kind = data["event"]
            if kind not in EVENTS:
                continue
            us = int(data["us"])
            phase = data["phase"] if kind != "present" else ""
            ticket = int(data["ticket"]) if kind in ("picture", "present") else 0
            counters, phases = {}, {}
            if kind == "picture":
                for channel in channels:
                    count, changes = int(data[channel]), int(data[f"{channel}_change"])
                    if not 0 <= changes <= count:
                        raise ValueError(f"invalid {channel} revision counters")
                    counters[channel] = count, changes
                    phases[channel] = data.get(f"{channel}_phase", "")
            if us < 0 or (kind in ("picture", "present") and ticket <= 0):
                raise ValueError("invalid timestamp or ticket")
        except (KeyError, ValueError) as exc:
            raise ValueError(f"line {number}: malformed Ordon telemetry ({exc})") from exc
        events.append(Event(kind, us, phase, ticket, counters, phases))
    # CPU and render threads can acquire the log lock out of timestamp order.
    return sorted(events, key=lambda event: event.us)


def describe_intervals(values, hz):
    if not values:
        return "no intervals"
    period = MICROSECONDS / hz
    ordered = sorted(values)
    p95 = ordered[(95 * (len(ordered) - 1) + 50) // 100]
    bursts = sum(delta < period / 2 for delta in values)
    gaps = sum(delta > period * 1.5 for delta in values)
    return (
        f"n={len(values)}, min/median/p95/max "
        f"{ordered[0] / 1000:.2f}/{median(ordered) / 1000:.2f}/"
        f"{p95 / 1000:.2f}/{ordered[-1] / 1000:.2f} ms, "
        f"{bursts} short (<{period / 2000:.2f} ms), "
        f"{gaps} long (>{period * 1.5 / 1000:.2f} ms)"
    )


def intervals(events):
    return [after.us - before.us for before, after in zip(events, events[1:])]


def consumed_paired_state(picture):
    expected = {"main": "half", "half": "main"}.get(picture.phase)
    return expected is not None and all(
        not phase or phase == expected for phase in picture.phases.values()
    )


def report_presents(pictures, presents, window_pictures, hz, channels=CHANNELS):
    matched = [present for present in presents if present.ticket in pictures]
    presentation_counts = Counter(present.ticket for present in presents)
    missing = 0
    if matched:
        low = min(present.ticket for present in matched)
        high = max(present.ticket for present in matched)
        missing = sum(
            low < ticket < high and not presentation_counts[ticket] for ticket in window_pictures
        )
    repeats = sum(count - 1 for count in presentation_counts.values())
    print(
        f"Present tickets: {len(matched)}/{len(presents)} matched, {missing} interior picture drops, "
        f"{repeats} duplicate presents"
    )
    print(f"Present cadence: {describe_intervals(intervals(presents), hz)}")

    results = defaultdict(Counter)
    consecutive = phase_breaks = gaps = joint = streak = longest = 0
    run_start = best_start = best_end = None
    period = MICROSECONDS / hz
    for before, after in zip(presents, presents[1:]):
        old = pictures.get(before.ticket)
        new = pictures.get(after.ticket)
        if old is None or new is None or after.ticket != before.ticket + 1:
            streak = 0
            gaps += 1
            continue
        consecutive += 1
        fresh = []
        for channel in channels:
            old_updates, old_changes = old.counters[channel]
            new_updates, new_changes = new.counters[channel]
            updates, changes = new_updates - old_updates, new_changes - old_changes
            result = results[channel]
            result["updated"] += updates > 0
            result["changed"] += changes > 0
            result["multiple"] += updates > 1
            result["regressed"] += updates < 0 or changes < 0
            new_once = updates == changes == 1
            result["new_once"] += new_once
            fresh.append(new_once)
        # The painter consumes the preceding pass's state. A phase label on
        # the picture is therefore expected to differ from the consumed update.
        paired = old.phase != new.phase and consumed_paired_state(old) and consumed_paired_state(new)
        if hz == 60 and not paired:
            phase_breaks += 1
        if all(fresh):
            joint += 1
        paced = period / 2 <= after.us - before.us <= period * 1.5
        if not all(fresh) or not paced or (hz == 60 and not paired):
            streak = 0
            continue
        if streak == 0:
            run_start = before
        streak += 1
        if streak > longest:
            longest = streak
            best_start, best_end = run_start, after

    print(f"Consecutive matched ticket pairs: {consecutive}/{max(0, len(presents) - 1)} ({gaps} breaks)")
    for channel in channels:
        result = results[channel]
        print(
            f"  {channel:6} updated {result['updated']}/{consecutive}, changed {result['changed']}/{consecutive}, "
            f"exactly one new value {result['new_once']}/{consecutive}, "
            f"multiple updates {result['multiple']}, counter regressions {result['regressed']}"
        )
    print(f"Joint new {'/'.join(channels)}: {joint}/{consecutive} consecutive ticket pairs")
    if hz == 60:
        print(f"Experimental phase breaks: {phase_breaks}/{consecutive} consecutive ticket pairs")
    print(f"Longest cadence-matched joint run: {longest} consecutive pairs", end="")
    if best_start is not None and best_end is not None:
        seconds = (best_end.us - best_start.us) / MICROSECONDS
        print(f" at {longest / seconds:.2f}/s (tickets {best_start.ticket} through {best_end.ticket})", end="")
    print()
    return longest


def report(path, label, hz, start, end, channels=CHANNELS):
    with path.open(encoding="utf-8", errors="replace") as log:
        events = parse(log, channels)
    if not events:
        raise ValueError(f"{path}: no Ordon telemetry")
    origin = events[0].us
    selected = [event for event in events if start <= (event.us - origin) / MICROSECONDS <= end]
    if len(selected) < 2 or selected[-1].us == selected[0].us:
        raise ValueError(f"{path}: no measurable events in the requested window")

    span = (selected[-1].us - selected[0].us) / MICROSECONDS
    kinds = defaultdict(list)
    for event in selected:
        kinds[event.kind].append(event)
    print(f"{label} | {path} | {span:.3f} s of Ordon telemetry")
    for kind in EVENTS:
        entries = kinds[kind]
        phases = Counter(event.phase for event in entries if event.phase)
        breakdown = ", ".join(f"{phase} {count}" for phase, count in sorted(phases.items()))
        print(f"  {kind:9} {len(entries):5}  {len(entries) / span:6.2f}/s" + (f"  ({breakdown})" if phases else ""))

    moves = kinds["move"]
    move_pairs = defaultdict(list)
    for previous, current in zip(moves, moves[1:]):
        move_pairs[previous.phase, current.phase].append(current.us - previous.us)
    print(f"Movement cadence: {describe_intervals(intervals(moves), hz)}")
    for (previous, current), values in sorted(move_pairs.items()):
        print(f"  {previous} -> {current}: {describe_intervals(values, hz)}")

    pictures = {}
    for event in events:
        if event.kind != "picture":
            continue
        if event.ticket in pictures:
            raise ValueError(f"{path}: repeated picture ticket {event.ticket}")
        pictures[event.ticket] = event
    selected_tickets = {event.ticket for event in kinds["picture"]}
    return report_presents(pictures, kinds["present"], selected_tickets, hz, channels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    captures = Path(__file__).resolve().parents[2] / "build" / "true60-validation"
    parser.add_argument("stock", nargs="?", type=Path, default=captures / "stock" / "route.log")
    parser.add_argument("experiment", nargs="?", type=Path, default=captures / "true60" / "route.log")
    parser.add_argument("--start", type=float, default=0, help="Start seconds after each capture's first Ordon event")
    parser.add_argument("--end", type=float, default=float("inf"), help="End seconds after each capture's first Ordon event")
    parser.add_argument("--min-run", type=int, default=0, help="Require this many consecutive paced experimental pairs with new move, camera and pose values")
    parser.add_argument("--require-skeleton", action="store_true",
                        help="Also require a fresh measured leg configuration on each qualifying present")
    args = parser.parse_args()
    if not 0 <= args.start < args.end or args.min_run < 0:
        parser.error("expected 0 <= start < end and a nonnegative minimum run")
    try:
        channels = CHANNELS + ("skeleton",) if args.require_skeleton else CHANNELS
        report(args.stock, "Stock", 30, args.start, args.end, channels)
        print()
        longest = report(args.experiment, "Experiment", 60, args.start, args.end, channels)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{exc}\n")
    if longest < args.min_run:
        parser.exit(1, f"Experimental run had {longest} qualifying pairs, fewer than the required {args.min_run}\n")


if __name__ == "__main__":
    main()

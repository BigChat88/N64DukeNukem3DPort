#!/usr/bin/env python3
"""
perf_report.py -- tables from the game's debug log (see "Measuring
performance" in the README).

    python tools/perf_report.py run.log              # one run
    python tools/perf_report.py before.log after.log # what changed, per level
    python tools/perf_report.py run.log --csv run.csv

It reads:
  * "bench: E1L1 frames=... avg=...ms ..."  one line per level (make BENCH=s)
  * "memory E1L1: heap used ..."            a level starts (every build)
  * "fps: N" and "  ms/frame: a=1.0 ..."    once per second (make PROFILE=1)
  * "  con op ..."                          the costliest CON opcodes (PROFILE)
  * "  wall columns on the CPU: ..."        walls the RDP could not draw (PROFILE)

The first seconds of each level (fade in, first loads) are left out of the
per-second figures, as the bench lines leave out their first 3 s.
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import OrderedDict, defaultdict

SETTLE_SECONDS = 3

RE_LEVEL = re.compile(r"memory (E\d+L\d+): heap used (\d+) KB, free (\d+) KB")
RE_BENCH = re.compile(r"bench: (E\d+L\d+) (.*)")
RE_FPS = re.compile(r"\bfps: (\d+)")
RE_MS = re.compile(r"ms/frame:((?: \w+=[\d.]+)+)")
RE_CON = re.compile(r"con op +\d+ (\S+) +([\d.]+) ms/frame +(\d+) calls")
RE_WALLS = re.compile(r"wall columns on the CPU: (.*) \(per frame\)")
RE_KV = re.compile(r"(\w+)=([\d.]+)")

# Sections shown in the per-level table (all of them go to the CSV). The
# others are parts of these (see src/n64/n64_prof.h).
MAIN_SECTIONS = ["logic", "rooms", "masks", "rest", "present", "audio", "con", "tileload", "sndload"]


class Level:
    def __init__(self, name: str):
        self.name = name
        self.heap_used = self.heap_free = None
        self.bench: dict | None = None
        self.seconds = 0                       # per-second reports seen
        self.fps: list[float] = []
        self.ms: dict[str, list[float]] = defaultdict(list)
        self.walls: dict[str, list[float]] = defaultdict(list)

    def mean_ms(self, section: str) -> float | None:
        v = self.ms.get(section)
        return sum(v) / len(v) if v else None


def parse(path: str):
    levels: "OrderedDict[str, Level]" = OrderedDict()
    con = defaultdict(lambda: [0.0, 0, 0])   # op -> [sum ms/frame, sum calls, reports]
    cur: Level | None = None

    def level(name: str) -> Level:
        if name not in levels:
            levels[name] = Level(name)
        return levels[name]

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_BENCH.search(line)
            if m:
                level(m.group(1)).bench = {k: float(v) for k, v in RE_KV.findall(m.group(2))}
                continue
            m = RE_LEVEL.search(line)
            if m:
                cur = level(m.group(1))
                cur.heap_used, cur.heap_free = int(m.group(2)), int(m.group(3))
                cur.seconds = 0
                continue
            if cur is None:
                continue
            m = RE_FPS.search(line)
            if m:
                cur.seconds += 1
                if cur.seconds > SETTLE_SECONDS:
                    cur.fps.append(float(m.group(1)))
                continue
            if cur.seconds <= SETTLE_SECONDS:
                continue
            m = RE_MS.search(line)
            if m:
                for k, v in RE_KV.findall(m.group(1)):
                    cur.ms[k].append(float(v))
                continue
            m = RE_CON.search(line)
            if m:
                c = con[m.group(1)]
                c[0] += float(m.group(2)); c[1] += int(m.group(3)); c[2] += 1
                continue
            m = RE_WALLS.search(line)
            if m:
                for k, v in RE_KV.findall(m.group(1)):
                    cur.walls[k].append(float(v))
    return levels, con


def fmt(v, digits=1) -> str:
    return "-" if v is None else f"{v:.{digits}f}"


def mean(v):
    return sum(v) / len(v) if v else None


def report(path: str, levels, con) -> None:
    print(f"## {path}\n")
    bench = [l for l in levels.values() if l.bench]
    if bench:
        print("### Frame times (make BENCH)\n")
        print("| Level | fps | avg ms | worst ms | >=30 fps | 20-30 | 15-20 | <15 fps |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|")
        for l in bench:
            b = l.bench
            n = max(b.get("frames", 0), 1)
            pct = lambda k: f"{100 * b.get(k, 0) / n:.0f}%"
            print(f"| {l.name} | {fmt(b.get('fps'))} | {fmt(b.get('avg'), 2)} | {fmt(b.get('worst'))} | "
                  f"{pct('fps30')} | {pct('fps20')} | {pct('fps15')} | {pct('slower')} |")
        fpss = [l.bench.get("fps", 0) for l in bench]
        print(f"\nMean of the levels: {sum(fpss) / len(fpss):.1f} fps; "
              f"slowest: {min(bench, key=lambda l: l.bench.get('fps', 0)).name} ({min(fpss):.1f} fps)\n")

    prof = [l for l in levels.values() if l.ms]
    if prof:
        print("### Time per frame by section, ms (make PROFILE=1)\n")
        print("| Level | fps | " + " | ".join(MAIN_SECTIONS) + " | heap free KB |")
        print("|---|---:|" + "---:|" * len(MAIN_SECTIONS) + "---:|")
        for l in prof:
            cells = [fmt(l.mean_ms(s)) for s in MAIN_SECTIONS]
            print(f"| {l.name} | {fmt(mean(l.fps))} | " + " | ".join(cells) + f" | {l.heap_free or '-'} |")
        # The sections that cost the most over all the levels.
        totals = defaultdict(list)
        for l in prof:
            for s in l.ms:
                totals[s].append(l.mean_ms(s))
        top = sorted(((sum(v) / len(v), s) for s, v in totals.items()), reverse=True)[:12]
        print("\nCostliest sections (mean of the levels, ms/frame; nested ones are parts of others):")
        print(", ".join(f"{s} {v:.1f}" for v, s in top) + "\n")
        walls = defaultdict(list)
        for l in prof:
            for k, v in l.walls.items():
                walls[k].append(mean(v))
        if walls:
            print("Wall columns drawn on the CPU instead of the RDP, per frame: " +
                  ", ".join(f"{k} {sum(v) / len(v):.0f}" for k, v in walls.items()) + "\n")
    if con:
        print("### Costliest CON opcodes (make PROFILE=1)\n")
        print("| opcode | ms/frame | calls/frame |")
        print("|---|---:|---:|")
        for op, (ms, calls, n) in sorted(con.items(), key=lambda kv: -kv[1][0] / kv[1][2])[:10]:
            print(f"| {op} | {ms / n:.2f} | {calls / n:.0f} |")
        print()
    if not bench and not prof:
        print("No bench or profile lines in this log: was it a BENCH= or PROFILE=1 build?\n")


def compare(a_path: str, a, b_path: str, b) -> None:
    print(f"## {a_path} -> {b_path}\n")
    # Levels measured in both logs: by their bench lines when the logs have
    # them (a level cut short has none), else by the per-second fps.
    benched = any(l.bench for l in a.values()) or any(l.bench for l in b.values())
    names = [n for n in a if n in b and (not benched or (a[n].bench and b[n].bench))]
    if not names:
        print("No level in common.\n")
        return
    print("| Level | fps before | fps after | change | avg ms before | after |")
    print("|---|---:|---:|---:|---:|---:|")
    changes = []
    for n in names:
        fa = a[n].bench.get("fps") if a[n].bench else mean(a[n].fps)
        fb = b[n].bench.get("fps") if b[n].bench else mean(b[n].fps)
        ma = a[n].bench.get("avg") if a[n].bench else None
        mb = b[n].bench.get("avg") if b[n].bench else None
        ch = (fb - fa) / fa * 100 if fa and fb else None
        if ch is not None:
            changes.append(ch)
        print(f"| {n} | {fmt(fa)} | {fmt(fb)} | {fmt(ch)}% | {fmt(ma, 2)} | {fmt(mb, 2)} |")
    if changes:
        print(f"\nMean change: {sum(changes) / len(changes):+.1f}% fps over {len(changes)} levels\n")
    secs = sorted({s for n in names for s in a[n].ms} & {s for n in names for s in b[n].ms})
    if secs:
        print("| Section (ms/frame, mean of the levels) | before | after | change |")
        print("|---|---:|---:|---:|")
        for s in secs:
            va = mean([a[n].mean_ms(s) for n in names if a[n].mean_ms(s) is not None])
            vb = mean([b[n].mean_ms(s) for n in names if b[n].mean_ms(s) is not None])
            if va is None or vb is None or max(va, vb) < 0.05:
                continue
            print(f"| {s} | {va:.2f} | {vb:.2f} | {vb - va:+.2f} |")
        print()


def write_csv(path: str, levels) -> None:
    sections = sorted({s for l in levels.values() for s in l.ms})
    bench_keys = ["fps", "avg", "worst", "frames", "fps30", "fps20", "fps15", "slower"]
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["level", "heap_used_kb", "heap_free_kb", "fps_profile"] +
                   [f"bench_{k}" for k in bench_keys] + [f"ms_{s}" for s in sections])
        for l in levels.values():
            w.writerow([l.name, l.heap_used, l.heap_free, fmt(mean(l.fps), 2)] +
                       [(l.bench or {}).get(k, "") for k in bench_keys] +
                       [fmt(l.mean_ms(s), 3) for s in sections])


def main() -> None:
    ap = argparse.ArgumentParser(description="Tables from the game's debug log (BENCH / PROFILE builds).")
    ap.add_argument("logs", nargs="+", help="one log, or two to compare (before after)")
    ap.add_argument("--csv", help="write every figure of the (last) log to this CSV file")
    args = ap.parse_args()
    if len(args.logs) > 2:
        ap.error("one or two logs")

    parsed = [(p, *parse(p)) for p in args.logs]
    for p, levels, con in parsed:
        report(p, levels, con)
    if len(parsed) == 2:
        compare(parsed[0][0], parsed[0][1], parsed[1][0], parsed[1][1])
    if args.csv:
        write_csv(args.csv, parsed[-1][1])
        print(f"CSV: {args.csv}")


if __name__ == "__main__":
    main()

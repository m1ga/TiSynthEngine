#!/usr/bin/env python3
"""Alternate already-built, own-platform review binaries; never opens devices.

Builds and exact-state regression must finish before this command is run.
All raw trials, CPU/elapsed distributions and process high-water memory stay in
the report. Thresholds trigger investigation, not automatic acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--mode", choices=["render", "producer", "android-producer", "prune"], default="render")
    parser.add_argument("--label", default="candidate")
    parser.add_argument("--case", type=int, action="append", help="Narrow a render kind or producer step count")
    parser.add_argument("--blocks", type=int, default=1000)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 10:
        parser.error("At least ten alternating pairs are required")
    if args.blocks < 1:
        parser.error("Positive block count required")
    cases = []
    if args.mode == "render":
        cases = [[str(kind), str(args.blocks)] for kind in (args.case or [0, 2, 3, 4, 5, 6, 7])]
    elif args.mode in ["producer", "android-producer"]:
        cases = [(["-2"] if args.mode == "producer" else []) + [str(steps), "100"] for steps in (args.case or [1, 512])]
    else:
        cases = [["-1", "100000"]]
    report = {"scope": "Apple ObjC renderer/producer and Android JNI producer on an ARM64 macOS host; no device acceptance or Java VM timing",
              "createdUnix": time.time(), "runs": args.runs, "physicalAcceptance": False, "cases": []}
    binaries = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}
    report["mode"] = args.mode
    report["binaries"] = {mode: hashlib.sha256(path.read_bytes()).hexdigest() for mode, path in binaries.items()}
    for inputs in cases:
        trials = {"baseline": [], "candidate": []}
        for pair in range(args.runs + 1):
            order = ["baseline", "candidate"] if pair % 2 == 0 else ["candidate", "baseline"]
            for mode in order:
                binary = binaries[mode]
                assert hashlib.sha256(binary.read_bytes()).hexdigest() == report["binaries"][mode]
                command = [str(binary)] + inputs
                row = json.loads(subprocess.check_output(command, text=True))
                if pair:
                    trials[mode].append(row)
        if "state" in trials["baseline"][0]:
            assert len({r["state"] for rows in trials.values() for r in rows}) == 1, "Producer state differs"
        keys = [key for key in trials["baseline"][0] if key not in ["kind", "blocks", "warmupBlocks", "steps", "loops", "state"]]
        medians = {mode: {k: statistics.median(r[k] for r in rows) for k in keys} for mode, rows in trials.items()}
        change = {k: 100 * (medians["candidate"][k] / medians["baseline"][k] - 1)
                  for k in keys if medians["baseline"][k] and k not in ["retainedAllocationBlocks", "retainedAllocationBytes"]}
        row = {"case": args.label, "arguments": inputs, "median": medians, "changePercent": change,
               "investigate": any(change.get(k, 0) > limit for k, limit in [("cpuSeconds", 2), ("wallSeconds", 2), ("cpuP95", 5), ("wallP95", 5)]),
               "trials": trials}
        report["cases"].append(row)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({k: row[k] for k in ["case", "arguments", "changePercent", "investigate"]}), flush=True)


if __name__ == "__main__":
    main()

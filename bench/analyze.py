#!/usr/bin/env python3
"""
Analysis of the benchmarks of the pipeline server.

usage: analyze.py <results1.csv> [<results2.csv> ...]
       analyze.py all            # every bench/results/*.csv available

It reads the raw runs (scenario,param,run,seconds), calculates the median of
each point, and reports:

  T(1)/T(p)  S(p), speedup
  E(p)       E(p) = S(p)/p, efficiency
  f          parallelizable fraction estimated by minimizing the distance
             between the S(p) measured and the one predicted by Amdahl,
             S(p) = 1 / ((1 - f) + f / p)

For the data scenario the param is the number of OpenMP threads of the task;
for wide, deep and combined it is the number of agents.
"""
import csv
import glob
import math
import os
import sys
from collections import defaultdict

SCENARIOS = ["wide", "deep", "data_wc", "data_hist", "combined"]


def medians(path):
    """scenario -> param -> sorted list of seconds; includes the metadata
    found in the first comment lines."""
    data = defaultdict(lambda: defaultdict(list))
    meta = []

    with open(path, newline="") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("#"):
                meta.append(line[2:])
                continue
            parts = line.split(",")
            if len(parts) != 4:
                continue
            scenario, param, run, seconds = parts
            try:
                data[scenario][int(param)].append(float(seconds))
            except (ValueError, KeyError):
                continue

    med = defaultdict(dict)
    for scenario, by_param in data.items():
        for param, values in by_param.items():
            values.sort()
            n = len(values)
            med[scenario][param] = (
                values[n // 2]
                if n % 2
                else (values[n // 2 - 1] + values[n // 2]) / 2.0
            )

    return med, meta


def amdahl_fit(points):
    """points: list of (p, S_measured) with p >= 1. It returns the f\
    that minimizes the sum of the squared relative errors between the
    measured S(p) and the one predicted by Amdahl."""
    best_f, best_error = None, None
    steps = 5000
    for i in range(1, steps + 1):
        f = i / (steps + 1)
        error = 0.0
        count = 0
        for p, s in points:
            if p <= 1:
                continue
            predicted = 1.0 / ((1 - f) + f / p)
            error += ((s - predicted) / max(s, predicted)) ** 2
            count += 1
        if count == 0:
            continue
        if best_error is None or error < best_error:
            best_error, best_f = error, f
    return best_f if best_f is not None else 0.0


def analyze(path):
    med, meta = medians(path)
    base = os.path.basename(path)
    out = []
    out.append("=" * 64)
    out.append(f"results: {base}")
    for line in meta:
        out.append(f"  {line}")
    out.append("")

    for scenario in SCENARIOS:
        if scenario not in med:
            continue
        points = sorted(med[scenario].items())
        if not points:
            continue

        out.append(f"scenario {scenario}")
        out.append(f"{'param':>8} {'median(s)':>12} {'S(p)':>8} {'E(p)':>8}")

        t1 = points[0][1]
        series = []

        for p, t in points:
            speedup = (t1 / t) if t > 0 else 0.0
            series.append((p, speedup))
            eff = (speedup / p) if p > 0 else 0.0
            out.append(f"{p:>8} {t:>12.3f} {speedup:>8.2f} {eff:>8.2f}")

        f_est = amdahl_fit(series)
        out.append(
            f"  parallelizable fraction (fit): f = {f_est:.3f} "
            "(the serial fraction is 1 - f). Measured vs Amdahl:"
        )
        for p, _ in series:
            if p <= 1:
                continue
            predicted = 1.0 / ((1 - f_est) + f_est / p)
            measured = dict(series)[p]
            out.append(
                f"    p={p}: measured {measured:.2f} | "
                f"Amdahl {predicted:.2f}"
            )
        out.append("")

    return "\n".join(out)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    if args == ["all"]:
        paths = sorted(glob.glob(os.path.join("bench", "results", "*.csv")))
        if not paths:
            print("there are no results in bench/results/")
            return 1
    else:
        paths = args

    for path in paths:
        if not os.path.exists(path):
            print(f"the file does not exist: {path}", file=sys.stderr)
            continue
        print(analyze(path))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())

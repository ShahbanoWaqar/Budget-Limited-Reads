#!/usr/bin/env python3
"""Summaries from the blr CSV logs.

  summarize.py study DIR          read DIR/manifest.csv, write DIR/per_run.csv and
                                  DIR/summary.csv (mean and std over repetitions), print a table
  summarize.py percentiles PREFIX print "n p50_us p90_us p95_us p99_us throughput_per_s"
                                  for one run (used by the calibration scripts)

A "run" is the set PREFIX.logical.csv, PREFIX.replica.csv, PREFIX.activity.csv.
Latency is measured from the scheduled arrival to the answer, so any waiting for
admission is included. Percentiles use numpy's default linear interpolation and
cover successful logical reads only; failures are reported in n_failed.
"""
import argparse
import os
import sys

import numpy as np
import pandas as pd


def load_run(prefix):
    logical = pd.read_csv(prefix + ".logical.csv")
    replica = pd.read_csv(prefix + ".replica.csv")
    activity_path = prefix + ".activity.csv"
    activity = pd.read_csv(activity_path) if os.path.exists(activity_path) else None
    return logical, replica, activity


def run_metrics(prefix):
    logical, replica, activity = load_run(prefix)
    ok = logical[logical["ok"] == 1]
    lat_ms = ok["latency_ns"].to_numpy() / 1e6
    m = {"n_logical": len(logical), "n_ok": len(ok), "n_failed": int((logical["ok"] != 1).sum())}
    if len(ok) == 0:
        return m
    for q in (50, 90, 95, 99):
        m[f"p{q}_ms"] = float(np.percentile(lat_ms, q))
    m["mean_ms"] = float(lat_ms.mean())
    span_s = (logical["answer_ns"].max() - logical["arrival_ns"].min()) / 1e9
    m["throughput_per_s"] = len(ok) / span_s if span_s > 0 else float("nan")
    m["replicas_total"] = len(replica)
    m["replicas_per_logical"] = len(replica) / len(ok)
    m["replica_bytes"] = int(replica["result"].clip(lower=0).sum())
    m["replica_failures"] = int((replica["verified"] != 1).sum())
    m["dup_fraction"] = float((logical["duplicated"] == 1).mean())

    dup_ids = logical.loc[logical["duplicated"] == 1, "logical_id"]
    m["dup_win_fraction"] = float("nan")
    m["loser_cleanup_ms_mean"] = float("nan")
    m["loser_cleanup_ms_p99"] = float("nan")
    if len(dup_ids):
        r = replica[replica["logical_id"].isin(dup_ids)]
        last_replica_id = r.groupby("logical_id")["replica_id"].max()
        winners = r[r["winner"] == 1].set_index("logical_id")["replica_id"]
        joined = pd.concat([winners.rename("win"), last_replica_id.rename("dup")], axis=1).dropna()
        if len(joined):
            m["dup_win_fraction"] = float((joined["win"] == joined["dup"]).mean())
        last_done = r.groupby("logical_id")["complete_ns"].max()
        ans = logical.set_index("logical_id")["answer_ns"].reindex(last_done.index)
        cleanup_ms = ((last_done - ans).clip(lower=0) / 1e6).to_numpy()
        if len(cleanup_ms):
            m["loser_cleanup_ms_mean"] = float(cleanup_ms.mean())
            m["loser_cleanup_ms_p99"] = float(np.percentile(cleanup_ms, 99))

    m["peak_outstanding"] = m["peak_permits"] = m["mean_permits"] = float("nan")
    if activity is not None and len(activity):
        m["peak_outstanding"] = int((activity["outstanding_a"] + activity["outstanding_b"]).max())
        m["peak_permits"] = int(activity["active_permits"].max())
        m["mean_permits"] = float(activity["active_permits"].mean())
    return m


KEYS = ["study", "scenario", "mode", "rate", "delay_us", "permits", "threshold"]
AGG = ["p50_ms", "p99_ms", "throughput_per_s", "replicas_per_logical", "dup_fraction",
       "dup_win_fraction", "loser_cleanup_ms_mean", "peak_permits", "n_failed"]


def cmd_study(directory):
    manifest = os.path.join(directory, "manifest.csv")
    if not os.path.exists(manifest):
        sys.exit(f"no manifest.csv in {directory}")
    runs = pd.read_csv(manifest, dtype=str)
    rows = []
    for _, row in runs.iterrows():
        try:
            m = run_metrics(row["prefix"])
        except FileNotFoundError as e:
            print(f"warning: skipping {row['prefix']}: {e}", file=sys.stderr)
            continue
        rows.append({**row.to_dict(), **m})
    if not rows:
        sys.exit("no runs could be read")
    per_run = pd.DataFrame(rows)
    per_run.to_csv(os.path.join(directory, "per_run.csv"), index=False)
    for c in AGG:
        per_run[c] = pd.to_numeric(per_run.get(c), errors="coerce")
    grouped = per_run.groupby(KEYS, dropna=False, sort=False)
    mean = grouped[AGG].mean()
    std = grouped[AGG].std(ddof=1).add_suffix("_std")
    summary = pd.concat([mean, std, grouped.size().rename("n_reps")], axis=1).reset_index()
    summary.to_csv(os.path.join(directory, "summary.csv"), index=False)
    show = summary[KEYS[1:] + ["p50_ms", "p99_ms", "p99_ms_std", "throughput_per_s",
                               "replicas_per_logical", "dup_fraction", "dup_win_fraction", "n_reps"]]
    with pd.option_context("display.width", 200, "display.max_columns", 30, "display.float_format", "{:.3f}".format):
        print(show.to_string(index=False))
    print(f"\nwrote {directory}/per_run.csv and {directory}/summary.csv")


def cmd_percentiles(prefix):
    logical = pd.read_csv(prefix + ".logical.csv")
    ok = logical[logical["ok"] == 1]
    if ok.empty:
        sys.exit("no successful reads")
    us = ok["latency_ns"].to_numpy() / 1e3
    span_s = (logical["answer_ns"].max() - logical["arrival_ns"].min()) / 1e9
    thr = len(ok) / span_s if span_s > 0 else 0.0
    p = [int(round(np.percentile(us, q))) for q in (50, 90, 95, 99)]
    print(len(ok), *p, f"{thr:.1f}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("study"); s.add_argument("dir")
    p = sub.add_parser("percentiles"); p.add_argument("prefix")
    a = ap.parse_args()
    cmd_study(a.dir) if a.cmd == "study" else cmd_percentiles(a.prefix)

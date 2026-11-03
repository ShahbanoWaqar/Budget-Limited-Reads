#!/usr/bin/env python3
"""Figures from a study's summary.csv (made by summarize.py).

  plot_studies.py results/raw/studyA [--out figs]

Writes, into --out (default DIR/figs):
  tradeoff.png   p99 latency vs replica reads per logical read (one panel per rate/scenario),
                 error bars are one standard deviation over repetitions
  bars.png       p99 latency, replica reads per logical read, and duplicate win fraction
                 for every configuration, grouped by scenario
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

COLORS = {"a": "#7f7f7f", "b": "#bcbd22", "rr": "#1f77b4", "lessbusy": "#17becf",
          "fixed": "#ff7f0e", "budget": "#2ca02c"}


def label(r):
    if r["mode"] == "fixed":
        return f"fixed {float(r['delay_us'])/1000:g} ms"
    if r["mode"] == "budget":
        thr = "no thr" if r["threshold"] == "off" else f"thr {r['threshold']}"
        return f"budget {float(r['delay_us'])/1000:g} ms, {r['permits']} permits, {thr}"
    return {"a": "drive A only", "b": "drive B only", "rr": "round-robin", "lessbusy": "less-busy"}.get(r["mode"], r["mode"])


def load(directory):
    df = pd.read_csv(os.path.join(directory, "summary.csv"), dtype={"delay_us": str, "permits": str, "threshold": str})
    df["label"] = df.apply(label, axis=1)
    df["panel"] = df["scenario"] + ", " + df["rate"].astype(str) + "/s"
    return df


def tradeoff(df, out):
    panels = list(dict.fromkeys(df["panel"]))
    fig, axes = plt.subplots(1, len(panels), figsize=(5.2 * len(panels), 4.2), squeeze=False)
    for ax, panel in zip(axes[0], panels):
        d = df[df["panel"] == panel]
        for _, r in d.iterrows():
            ax.errorbar(r["replicas_per_logical"], r["p99_ms"],
                        yerr=0 if np.isnan(r["p99_ms_std"]) else r["p99_ms_std"],
                        marker="o", ms=7, capsize=3, color=COLORS.get(r["mode"], "k"), label=r["label"])
        ax.set_title(panel); ax.set_xlabel("replica reads per logical read"); ax.set_ylabel("p99 latency (ms)")
        ax.grid(alpha=.3); ax.legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(out, "tradeoff.png"), dpi=200); plt.close(fig)


def bars(df, out):
    scen = list(dict.fromkeys(df["panel"]))
    labels = list(dict.fromkeys(df["label"]))
    fig, axes = plt.subplots(3, 1, figsize=(max(7, 1.4 * len(scen) * 2), 9), sharex=True)
    w = 0.8 / max(len(labels), 1)
    for ax, (col, std, title) in zip(axes, [("p99_ms", "p99_ms_std", "p99 latency (ms)"),
                                            ("replicas_per_logical", None, "replica reads per logical read"),
                                            ("dup_win_fraction", None, "duplicate win fraction")]):
        for i, lab in enumerate(labels):
            ys, es = [], []
            for s in scen:
                row = df[(df["panel"] == s) & (df["label"] == lab)]
                ys.append(row[col].iloc[0] if len(row) else np.nan)
                es.append(row[std].iloc[0] if (len(row) and std) else 0)
            mode = df[df["label"] == lab]["mode"].iloc[0]
            ax.bar(np.arange(len(scen)) + i * w, np.nan_to_num(ys), w, yerr=np.nan_to_num(es), capsize=2,
                   color=COLORS.get(mode, "k"), alpha=0.55 + 0.4 * (i % 2) / 1, label=lab)
        ax.set_ylabel(title); ax.grid(axis="y", alpha=.3)
    axes[-1].set_xticks(np.arange(len(scen)) + w * (len(labels) - 1) / 2)
    axes[-1].set_xticklabels(scen, rotation=15)
    axes[0].legend(fontsize=6, ncol=2)
    fig.tight_layout(); fig.savefig(os.path.join(out, "bars.png"), dpi=200); plt.close(fig)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir"); ap.add_argument("--out")
    a = ap.parse_args()
    out = a.out or os.path.join(a.dir, "figs"); os.makedirs(out, exist_ok=True)
    d = load(a.dir)
    tradeoff(d, out); bars(d, out)
    print("wrote", os.path.join(out, "tradeoff.png"), "and", os.path.join(out, "bars.png"))

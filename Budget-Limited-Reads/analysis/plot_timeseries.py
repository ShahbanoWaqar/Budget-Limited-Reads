#!/usr/bin/env python3
"""Time series for one run: outstanding reads and permits, and latency over time.

  plot_timeseries.py PREFIX [--out file.png]

PREFIX is the run's path without the .logical.csv suffix. If PREFIX.bgB_ns exists
(Study C) a dashed line marks when interference started on drive B.
Latency panel: dots are logical reads, the line is the p99 of each 1-second window.
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("prefix"); ap.add_argument("--out")
a = ap.parse_args()
lg = pd.read_csv(a.prefix + ".logical.csv")
act = pd.read_csv(a.prefix + ".activity.csv")
marker = None
if os.path.exists(a.prefix + ".bgB_ns") and os.path.exists(a.prefix + ".start_ns"):
    marker = (int(open(a.prefix + ".bgB_ns").read()) - int(open(a.prefix + ".start_ns").read())) / 1e9

fig, ax = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
t = act["time_ns"] / 1e9
ax[0].step(t, act["outstanding_a"], where="post", label="outstanding on A")
ax[0].step(t, act["outstanding_b"], where="post", label="outstanding on B")
ax[0].step(t, act["active_permits"], where="post", label="active pair permits", color="k", lw=1)
ax[0].set_ylabel("count"); ax[0].legend(fontsize=8); ax[0].grid(alpha=.3)
ok = lg[lg["ok"] == 1]
ts = ok["answer_ns"] / 1e9; lat = ok["latency_ns"] / 1e6
ax[1].scatter(ts, lat, s=4, alpha=.35)
sec = np.floor(ts).astype(int)
p99 = lat.groupby(sec).apply(lambda s: np.percentile(s, 99))
ax[1].plot(p99.index + 0.5, p99.values, color="r", label="p99 per second")
ax[1].set_xlabel("time since start (s)"); ax[1].set_ylabel("logical latency (ms)")
ax[1].legend(fontsize=8); ax[1].grid(alpha=.3)
if marker is not None:
    for x in ax:
        x.axvline(marker, ls="--", color="gray")
    ax[1].text(marker, ax[1].get_ylim()[1] * 0.95, " load starts on B", fontsize=8, va="top")
fig.tight_layout()
out = a.out or a.prefix + ".timeseries.png"
fig.savefig(out, dpi=200); print("wrote", out)

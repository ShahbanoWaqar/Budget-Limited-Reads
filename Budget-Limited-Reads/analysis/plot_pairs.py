#!/usr/bin/env python3
"""One trace showing why an answer can leave extra work outstanding.

  plot_pairs.py PREFIX [--n 15] [--out file.png]

Draws the first N hedged reads of a run. Each row is one logical read: a bar per replica
read from its submission to its completion (colored by drive) and a black tick where the
application got its answer. Bars that extend past the tick are losing reads still running.
"""
import argparse

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("prefix"); ap.add_argument("--n", type=int, default=15); ap.add_argument("--out")
a = ap.parse_args()
lg = pd.read_csv(a.prefix + ".logical.csv"); rp = pd.read_csv(a.prefix + ".replica.csv")
ids = lg[(lg["duplicated"] == 1) & (lg["ok"] == 1)]["logical_id"].head(a.n).tolist()
if not ids:
    raise SystemExit("no hedged reads in this run")
col = {0: "#1f77b4", 1: "#2ca02c"}
fig, ax = plt.subplots(figsize=(9, 0.42 * len(ids) + 1.5))
for row, lid in enumerate(ids):
    ans = lg.loc[lg["logical_id"] == lid, "answer_ns"].iloc[0]
    reps = rp[rp["logical_id"] == lid].sort_values("replica_id")
    t0 = reps["submit_ns"].min()
    for j, (_, r) in enumerate(reps.iterrows()):
        y = row + (j - 0.5) * 0.35
        ax.barh(y, (r["complete_ns"] - r["submit_ns"]) / 1e6, left=(r["submit_ns"] - t0) / 1e6,
                height=0.3, color=col[int(r["drive"])], edgecolor="k" if r["winner"] == 1 else "none", lw=1.2)
    ax.plot([(ans - t0) / 1e6] * 2, [row - 0.45, row + 0.45], color="k", lw=2)
ax.set_yticks(range(len(ids))); ax.set_yticklabels([f"read {i}" for i in ids], fontsize=7)
ax.invert_yaxis(); ax.set_xlabel("time since the read's first submission (ms)")
ax.legend(handles=[plt.Rectangle((0, 0), 1, 1, color=col[0]), plt.Rectangle((0, 0), 1, 1, color=col[1]),
                   plt.Line2D([0], [0], color="k", lw=2)],
          labels=["drive A read", "drive B read", "answer returned"], fontsize=8, loc="lower right")
fig.tight_layout(); out = a.out or a.prefix + ".pairs.png"; fig.savefig(out, dpi=200); print("wrote", out)

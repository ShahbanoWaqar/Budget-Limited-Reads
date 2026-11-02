"""End-to-end trace replay, log reconciliation, and invalid argument checks."""
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

def rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))

with tempfile.TemporaryDirectory(prefix="blr-smoke-") as tmp:
    root = Path(tmp)
    data_a, data_b = root / "a.bin", root / "b.bin"
    for path in (data_a, data_b):
        subprocess.run([sys.argv[3], "8192", str(path)], check=True)
    assert data_a.read_bytes() == data_b.read_bytes()
    def expected(offset):
        mask = (1 << 64) - 1
        x = (offset + 0x9e3779b97f4a7c15) & mask
        x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & mask
        x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & mask
        return (x ^ (x >> 31)) & 255
    assert data_a.read_bytes() == bytes(expected(i) for i in range(8192))
    assert subprocess.run([sys.argv[3], "8192", str(data_a)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
    assert data_a.read_bytes() == data_b.read_bytes()
    trace = root / "trace.csv"
    subprocess.run([sys.argv[2], "65536", "100", "1", "42", str(trace)], check=True)
    for mode in ("rr", "lessbusy", "fixed", "budget"):
        prefix = root / mode
        subprocess.run([sys.argv[1], "--fake", "--mode", mode, "--trace", str(trace),
                        "--delay-us", "10", "--max-outstanding", "2", "--permits", "1",
                        "--out-prefix", str(prefix)], check=True)
        logical = rows(str(prefix) + ".logical.csv")
        replica = rows(str(prefix) + ".replica.csv")
        activity = rows(str(prefix) + ".activity.csv")
        assert len(logical) == 100 and all(int(r["ok"]) for r in logical)
        assert len({r["logical_id"] for r in logical}) == 100
        assert len({r["replica_id"] for r in replica}) == len(replica)
        assert sum(int(r["winner"]) for r in replica) == 100
        assert all(int(r["verified"]) for r in replica)
        assert all(sum(r["logical_id"] == l["logical_id"] for r in replica) in (1, 2) for l in logical)
        assert len(replica) == (100 if mode in ("rr", "lessbusy") else 200)
        assert all(int(r["outstanding_a"])+int(r["outstanding_b"]) <= 2 for r in activity)
        assert all(int(r["active_permits"]) <= 1 for r in activity)
        assert int(activity[-1]["replicas_completed"]) == len(replica)
        assert int(activity[-1]["outstanding_a"]) == int(activity[-1]["outstanding_b"]) == 0
        assert int(activity[-1]["active_permits"]) == 0
    assert subprocess.run([sys.argv[1], "--mode", "invalid", "--trace", str(trace)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
print("All four CLI modes and CSV accounting passed")

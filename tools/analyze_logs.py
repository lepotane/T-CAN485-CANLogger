#!/usr/bin/env python3
"""
analyze_logs.py - offline analysis for the T-CAN485 ultra-detailed CAN logger.

Reads a session folder (or a whole SD card root) produced by the firmware:
    sidNNNNN/
        pid_XXX.csv     decoded OBD-II PIDs (wide format)
        raw_XXX.bin     every raw CAN frame (18-byte records)
        diag_XXX.csv    DTC + Mode-06 snapshots
        info.txt        session metadata

Produces:
    - cold-start event detection (engine off -> running while coolant is cold)
    - idle-roughness / misfire proxy from RPM variability
    - per-event plots of the key tuning signals
    - a merged, analysis-ready CSV
    - a raw-CAN ID frequency report (to find Mercedes proprietary signals)

Usage:
    python analyze_logs.py path/to/sid00001 --out out
    python analyze_logs.py path/to/card_root --out out      (scans all sid*)
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RAW_RECORD_LEN = 18
RAW_MAGIC = b"TC485RAW"

# Signals we always try to plot for a cold-start event.
PLOT_COLS = [
    ("ect_c", "Coolant temp (degC)"),
    ("rpm", "Engine speed (rpm)"),
    ("maf_gps", "Air mass (g/s)"),
    ("map_kpa", "MAP (kPa)"),
    ("engine_load_pct", "Load (%)"),
    ("throttle_pct", "Throttle (%)"),
    ("pedal_d_pct", "Pedal (%)"),
    ("stft_b1_pct", "STFT (%)"),
    ("ltft_b1_pct", "LTFT (%)"),
    ("o2_b1s1_equiv", "Lambda B1S1"),
    ("fuel_press_kpa", "Fuel pressure (kPa)"),
    ("timing_adv_deg", "Timing advance (deg)"),
]


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------
def load_pid_csvs(root: Path) -> pd.DataFrame:
    files = sorted(root.glob("pid_*.csv"))
    if not files:
        return pd.DataFrame()
    frames = []
    for f in files:
        try:
            df = pd.read_csv(f)
            df["_src"] = f.name
            frames.append(df)
        except Exception as e:  # noqa: BLE001
            print(f"  ! could not read {f.name}: {e}")
    if not frames:
        return pd.DataFrame()
    df = pd.concat(frames, ignore_index=True)

    # Split into continuous segments where millis() resets (deep-sleep wakes).
    if "ms" in df.columns:
        reset = (df["ms"].diff() < 0).cumsum()
        df["_segment"] = reset
    else:
        df["_segment"] = 0

    for c in ("ms", "unix_ms"):
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    num_cols = [c for c in df.columns if c not in ("_src", "_segment", "ms", "unix_ms")]
    for c in num_cols:
        df[c] = pd.to_numeric(df[c], errors="coerce")
    return df


def load_raw_bins(root: Path):
    """Return a DataFrame of raw CAN frames: ts_ms, id, ext, rtr, dlc, b0..b7."""
    rows = []
    for f in sorted(root.glob("raw_*.bin")):
        data = f.read_bytes()
        if len(data) < 16 or data[:8] != RAW_MAGIC:
            print(f"  ! {f.name}: bad header, skipping")
            continue
        # 16-byte header, then 18-byte records
        off = 16
        while off + RAW_RECORD_LEN <= len(data):
            rec = data[off:off + RAW_RECORD_LEN]
            ts, can_id = struct.unpack("<II", rec[0:8])
            dlc = rec[8]
            flags_ext = (can_id >> 31) & 1
            flags_rtr = (can_id >> 30) & 1
            cid = can_id & 0x1FFFFFFF
            row = {"ts_ms": ts, "id": cid, "ext": flags_ext, "rtr": flags_rtr, "dlc": dlc}
            for i in range(8):
                row[f"b{i}"] = rec[10 + i]
            rows.append(row)
            off += RAW_RECORD_LEN
    return pd.DataFrame(rows)


def load_diag(root: Path) -> pd.DataFrame:
    files = sorted(root.glob("diag_*.csv"))
    rows = []
    for f in files:
        for line in f.read_text(errors="replace").splitlines():
            parts = line.split(",")
            if len(parts) >= 2:
                rows.append({"file": f.name, "line": line, "fields": parts})
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# Event detection
# ---------------------------------------------------------------------------
def detect_starts(df: pd.DataFrame, cold_ect=40.0):
    """Return a list of start-event dicts across all segments."""
    events = []
    if df.empty or "rpm" not in df.columns:
        return events
    for seg, g in df.groupby("_segment"):
        g = g.sort_values("ms").reset_index(drop=True)
        rpm = g["rpm"].fillna(0)
        ect = g["ect_c"] if "ect_c" in g.columns else pd.Series([np.nan] * len(g))
        running = rpm > 150
        # rising edge of "running"
        edges = running.astype(int).diff()
        starts = list(g.index[edges == 1])
        for idx in starts:
            pre = g.loc[:idx - 1]
            pre_running = pre["rpm"].fillna(0).max() if len(pre) else 0
            if pre_running > 150:
                continue
            ect_val = ect.loc[:idx].iloc[-1] if len(ect.loc[:idx]) else np.nan
            cold = bool(ect_val < cold_ect) if not np.isnan(ect_val) else False
            events.append({
                "segment": seg,
                "index": idx,
                "ms": g.loc[idx, "ms"] if "ms" in g.columns else idx,
                "ect_c": ect_val,
                "cold": cold,
                "frame": g,
            })
    return events


def idle_roughness(g: pd.DataFrame, window_s=1.0):
    """RPM standard deviation in a rolling window when idling (speed==0).
    A proxy for misfire / rough running. Returns a Series aligned to g."""
    if "rpm" not in g.columns or "ms" not in g.columns:
        return pd.Series([np.nan] * len(g))
    rpm = g["rpm"].fillna(0)
    speed = g["speed_kmh"].fillna(0) if "speed_kmh" in g.columns else pd.Series([0] * len(g))
    ms = g["ms"]
    rough = pd.Series([np.nan] * len(g))
    if len(g) < 2:
        return rough
    dt = ms.diff().median()
    if not dt or np.isnan(dt) or dt <= 0:
        dt = 50.0
    win = max(1, int(window_s * 1000 / dt))
    rstd = rpm.rolling(win, min_periods=max(2, win // 2)).std()
    idle = (speed < 1.0) & (rpm > 0)
    rough[idle] = rstd[idle]
    return rough


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------
def plot_event(df, ev, out_png, pre_s=5, post_s=180):
    g = ev["frame"]
    ms = g["ms"]
    t0 = ms.loc[ev["index"]]
    lo = t0 - pre_s * 1000
    hi = t0 + post_s * 1000
    w = g[(ms >= lo) & (ms <= hi)].copy()
    if w.empty:
        return
    t = (w["ms"] - t0) / 1000.0

    cols = [(c, lbl) for (c, lbl) in PLOT_COLS if c in w.columns]
    n = len(cols)
    if n == 0:
        return
    fig, axes = plt.subplots(n, 1, sharex=True, figsize=(12, 2.1 * n))
    if n == 1:
        axes = [axes]
    for ax, (c, lbl) in zip(axes, cols):
        ax.plot(t, w[c], lw=1.0)
        ax.set_ylabel(lbl, fontsize=8)
        ax.grid(alpha=0.3)
        ax.axvline(0, color="r", ls="--", lw=1)
    axes[0].set_title(
        f"Cold start event @ {ev['ms']:.0f} ms  (ECT={ev['ect_c']:.1f} degC)", fontsize=10)
    axes[-1].set_xlabel("time from start (s)")
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Raw CAN report
# ---------------------------------------------------------------------------
def raw_report(raw: pd.DataFrame, out_txt: Path):
    if raw.empty:
        return "no raw frames"
    cnt = raw.groupby("id").size().sort_values(ascending=False)
    lines = ["# Raw CAN ID frequency (id: frames, ext/rtr, dlc)"]
    dlc_map = raw.groupby("id")["dlc"].agg(lambda s: s.mode().iloc[0])
    ext_map = raw.groupby("id")["ext"].max()
    for cid, n in cnt.items():
        lines.append(f"0x{cid:03X}: {n:>8}  ext={ext_map[cid]} dlc={int(dlc_map[cid])}")
    txt = "\n".join(lines)
    out_txt.write_text(txt)
    return txt


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def analyze_session(sess: Path, out: Path, post_s: int, cold_ect: float):
    print(f"\n=== session {sess.name} ===")
    out.mkdir(parents=True, exist_ok=True)

    df = load_pid_csvs(sess)
    raw = load_raw_bins(sess)
    diag = load_diag(sess)

    print(f"  pid rows      : {len(df)}")
    print(f"  raw frames    : {len(raw)}")
    print(f"  diag lines    : {len(diag)}")

    if not df.empty:
        merged = out / f"{sess.name}_merged.csv"
        df.to_csv(merged, index=False)
        print(f"  merged csv    : {merged}")

        events = detect_starts(df, cold_ect)
        print(f"  start events  : {len(events)} "
              f"(cold: {sum(e['cold'] for e in events)})")
        for i, ev in enumerate(events):
            kind = "COLD" if ev["cold"] else "warm"
            print(f"    #{i}: t={ev['ms']:.0f} ms  ECT={ev['ect_c']:.1f}C  [{kind}]")
            plot_event(df, ev, out / f"{sess.name}_start{i}_{kind}.png", post_s=post_s)

        # roughness summary
        for seg, g in df.groupby("_segment"):
            g = g.sort_values("ms").reset_index(drop=True)
            r = idle_roughness(g)
            if r.notna().any():
                peak = r.max()
                print(f"  segment {seg}: peak idle RPM-std = {peak:.2f} rpm")

    if not raw.empty:
        txt = raw_report(raw, out / f"{sess.name}_raw_ids.txt")
        print(f"  raw id report : {out / (sess.name + '_raw_ids.txt')}")

    if not diag.empty:
        dtc = diag[diag["fields"].str[1].isin(["stored", "pending", "permanent"])]
        found = []
        for _, row in diag.iterrows():
            f = row["fields"]
            if len(f) >= 4 and f[2] == "DTC":
                found.append((f[1], f[3]))
        if found:
            print("  DTCs:")
            for svc, code in found:
                print(f"    [{svc}] {code}")
        else:
            print("  DTCs: none decoded")

    return df


def find_sessions(root: Path):
    if (root / "info.txt").exists() or list(root.glob("pid_*.csv")):
        return [root]
    subs = sorted([d for d in root.iterdir() if d.is_dir() and d.name.startswith("sid")])
    return subs if subs else [root]


def main():
    ap = argparse.ArgumentParser(description="T-CAN485 log analyzer")
    ap.add_argument("path", help="session folder (sidNNNNN) or SD root")
    ap.add_argument("--out", default="analysis_out", help="output folder")
    ap.add_argument("--post", type=int, default=180, help="seconds after start to plot")
    ap.add_argument("--cold-ect", type=float, default=40.0,
                    help="coolant temp below which a start counts as 'cold'")
    args = ap.parse_args()

    root = Path(args.path)
    if not root.exists():
        print(f"path not found: {root}")
        sys.exit(1)

    out = Path(args.out)
    sessions = find_sessions(root)
    print(f"found {len(sessions)} session folder(s)")
    for sess in sessions:
        analyze_session(sess, out, args.post, args.cold_ect)
    print(f"\ndone -> {out.resolve()}")


if __name__ == "__main__":
    main()
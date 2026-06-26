#!/usr/bin/env python3
"""Analyse an SC1000 plugin trace (from `make trace`) for jog-touch mis-decisions.

The trace logs, per audio block, the firmware stream as decoded (rawTouch, jog,
playing) next to the plugin's decision (gate mode, motor, pitch). Because both are
on one clock they're already aligned. This script finds the patterns behind the
four reported symptoms and — crucially — splits each problem window by whether the
capacitive bit actually fired, which decides the fix:

  • cap bit NOT firing while you move  → hardware dropout (PIC sensor; host can't see the touch)
  • cap bit firing but gate disagrees  → a gate bug we CAN fix in TouchGate.h

Usage:  python3 trace_analyze.py trace.csv
"""
import csv
import sys
from collections import namedtuple

# --- heuristics (tune if needed) ---
JOG_MOVE = 1          # |rawJog| above this = the platter is being moved this block
MOVE_WINDOW = 0.050   # s: treat as "moving" if any movement within this lookback
MIN_WINDOW = 0.040    # s: ignore problem windows shorter than this (noise)
QUIET_JOG = 1         # |rawJog| <= this = effectively stationary


def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    if not rows:
        sys.exit(f"{path}: no rows (did the app flush on quit?)")
    out = []
    R = namedtuple("R", "t n rawJog jog rawTouch playing motorSpeed motorStopped mode engaged pitch playheadSec")
    for r in rows:
        out.append(R(float(r["t"]), int(r["n"]), int(r["rawJog"]), int(r["jog"]),
                     int(r["rawTouch"]), int(r["playing"]), float(r["motorSpeed"]),
                     int(r["motorStopped"]), int(r["mode"]), int(r["engaged"]),
                     float(r["pitch"]), float(r["playheadSec"])))
    return out


def moving_flags(rows):
    """Per-block 'platter is being moved', smeared over MOVE_WINDOW so the gaps
    between strokes don't read as 'stopped'."""
    flags = [abs(r.rawJog) > JOG_MOVE for r in rows]
    out = [False] * len(rows)
    last = -1e9
    for i, r in enumerate(rows):
        if flags[i]:
            last = r.t
        out[i] = (r.t - last) <= MOVE_WINDOW
    return out


def dur(rows, a, b):
    return rows[b].t - rows[a].t + rows[b].n / sr(rows)


def sr(rows):
    # t advances by n/rate; infer rate from two consecutive blocks
    for i in range(1, len(rows)):
        dt = rows[i].t - rows[i - 1].t
        if dt > 0:
            return rows[i - 1].n / dt
    return 48000.0


def pct(x, n):
    return f"{100.0*x/n:.1f}%" if n else "—"


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: trace_analyze.py trace.csv")
    rows = load(sys.argv[1])
    n = len(rows)
    rate = sr(rows)
    total = rows[-1].t + rows[-1].n / rate
    mv = moving_flags(rows)

    print(f"== trace: {sys.argv[1]} ==")
    print(f"blocks={n}  duration={total:.1f}s  ~rate={rate:.0f}Hz  ~block={rows[0].n} smp\n")

    touched = sum(r.rawTouch for r in rows)
    playing = sum(r.playing for r in rows)
    print(f"cap touched: {pct(touched,n)} of blocks   transport playing: {pct(playing,n)} of blocks")

    # at-rest false positives (symptom 3/4): cap=1 while motor stopped and not moving.
    rest_fp = [r for i, r in enumerate(rows) if r.rawTouch and r.motorStopped and not mv[i]]
    print(f"cap=1 at a standstill (no motion): {len(rest_fp)} blocks "
          f"({pct(len(rest_fp),n)}) — harmless (jog drives anyway) but indicates cap at-rest noise")

    # --- Symptom 1: moving while playing but the motor keeps control (Released) ---
    print("\n[1] moving the platter but the motor keeps running (mode=Released while you move, playing)")
    # moving-ness is per-index (smeared), so scan by index rather than windows(pred).
    segs, start = [], None
    for i, r in enumerate(rows):
        bad = r.playing and not r.engaged and mv[i]
        if bad and start is None:
            start = i
        elif not bad and start is not None:
            segs.append((start, i - 1)); start = None
    if start is not None:
        segs.append((start, n - 1))
    segs = [(a, b) for (a, b) in segs if dur(rows, a, b) >= MIN_WINDOW]
    if not segs:
        print("   none ✓")
    for a, b in segs:
        capon = sum(rows[i].rawTouch for i in range(a, b + 1))
        span = b - a + 1
        cause = "HARDWARE: cap not firing (forward-push dropout)" if capon < 0.5 * span \
                else "GATE BUG: cap WAS firing but gate stayed Released"
        print(f"   t={rows[a].t:6.2f}s  dur={dur(rows,a,b)*1000:5.0f}ms  cap-on={pct(capon,span):>5}  -> {cause}")

    # --- Symptom 2: false-positive halt — engaged while playing, cap=1, but no motion ---
    print("\n[2] running sample halted by a stationary touch (engaged+playing, cap=1, no motion)")
    segs, start = [], None
    for i, r in enumerate(rows):
        bad = r.playing and r.engaged and r.rawTouch and not mv[i]
        if bad and start is None:
            start = i
        elif not bad and start is not None:
            segs.append((start, i - 1)); start = None
    if start is not None:
        segs.append((start, n - 1))
    segs = [(a, b) for (a, b) in segs if dur(rows, a, b) >= MIN_WINDOW]
    if not segs:
        print("   none ✓")
    for a, b in segs:
        pmin = min(rows[i].pitch for i in range(a, b + 1))
        print(f"   t={rows[a].t:6.2f}s  dur={dur(rows,a,b)*1000:5.0f}ms  pitch {rows[a].pitch:+.2f}->{pmin:+.2f}  "
              f"(stationary touch pulled the running sample toward a halt)")

    # --- cap dropouts DURING active scratching (the forward-push dropout) ---
    print("\n[*] cap dropouts while actively moving (rawTouch 0 during motion) — the hardware limit")
    segs, start = [], None
    for i, r in enumerate(rows):
        bad = mv[i] and not r.rawTouch
        if bad and start is None:
            start = i
        elif not bad and start is not None:
            segs.append((start, i - 1)); start = None
    if start is not None:
        segs.append((start, n - 1))
    segs = [(a, b) for (a, b) in segs if dur(rows, a, b) >= MIN_WINDOW]
    if not segs:
        print("   none ✓")
    else:
        ds = sorted(dur(rows, a, b) * 1000 for a, b in segs)
        print(f"   {len(segs)} dropouts while moving — durations(ms): "
              f"min={ds[0]:.0f} median={ds[len(ds)//2]:.0f} max={ds[-1]:.0f}")
        print("   (the gate bridges these via jog motion; only a SETTLED jog hands back to the motor)")

    # --- release quality: after a FORWARD scratch, does the pitch hold its momentum
    #     toward the motor, or "spin out" (dip toward 0 = stop effect) before catching? ---
    print("\n[R] release quality after a forward scratch (spin-out = pitch dips toward 0 before the motor)")
    segs = []
    i = 0
    while i < n - 1:
        # a release = engaged → not engaged while playing
        if rows[i].engaged and not rows[i + 1].engaged and rows[i + 1].playing:
            pre = max((rows[j].pitch for j in range(max(0, i - 15), i + 1)), default=0.0)
            if pre > 0.4:  # was scratching forward
                # scan forward until pitch reaches ~motor (within 0.1) or 400ms passes
                j = i + 1
                lo = rows[i].pitch
                tlimit = rows[i].t + 0.4
                while j < n and rows[j].t < tlimit and rows[j].pitch < rows[j].motorSpeed - 0.1:
                    lo = min(lo, rows[j].pitch)
                    j += 1
                # a real LET-GO settles: the jog must go quiet afterwards. If it keeps
                # moving (a reversal / continued scratch) it's not a release — skip it.
                settled = max((abs(rows[k].rawJog) for k in range(i + 1, j)), default=0) <= 3
                segs.append((rows[i + 1].t, pre, lo, settled))
            i += 1
        else:
            i += 1
    letgos = [s for s in segs if s[3]]
    if not letgos:
        print(f"   no clean forward let-gos in this capture ({len(segs)} forward releases were reversals/continued scratches)")
    else:
        for t, pre, lo, _ in letgos[:12]:
            tag = "SPIN-OUT (stop effect)" if lo < 0.25 else ("dip" if lo < pre - 0.3 else "clean")
            print(f"   t={t:6.2f}s  scratch≈{pre:+.2f} → min pitch {lo:+.2f} before catching the motor  -> {tag}")
        bad = sum(1 for _, _, lo, _ in letgos if lo < 0.25)
        print(f"   {bad}/{len(letgos)} clean let-gos spun out toward a stop (want: hold momentum, slip up to 1.0)")

    print("\nLegend: mode 0=Released 1=Scratch 2=Coast. 'cause' above is the actionable bit —")
    print("GATE BUG windows are fixable in TouchGate.h; HARDWARE windows are PIC cap-sense limits")
    print("(a HARDWARE window is only a real problem if the platter was actually moving, not idle ±1 trickle).")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Tank sensor temperature compensation calibration tool.

Queries InfluxDB for recent raw_adc + air_temperature data, fits piecewise
linear slopes per temperature band, locates the warm/cold regime boundary,
and pushes the new coefficients to the device via Calibration.SetCoefficients.

Usage:
    python3 calibrate.py [--days N] [--device URL] [--dry-run] [--band-size F]
                         [--warming-only] [--min-r2 F]

The device RPC call made on success:
    POST /rpc/Calibration.SetCoefficients
    {"slope_cold": F, "slope_warm": F, "t_cold": F, "t_warm": F}
"""

import sys
import json
import argparse
import statistics
import urllib.request

# ── defaults ─────────────────────────────────────────────────────────────────
INFLUX_URL    = "http://epi4.eremia:8086"
INFLUX_TOKEN  = "Jk6gFmIBqx6XnKewK5hH30yQ7Jb6-YEa82ZTmJTwVy--pqfLYbx3xYA5UQsNqBN9Eh5Kkfe2ZbfyOp4DjbumRw=="
INFLUX_ORG    = "abe31a72e7bd3d64"
INFLUX_BUCKET = "tank-data"
DEVICE_URL    = "http://tanksensor2.eremia"

MIN_SAMPLES_PER_BAND = 30
MIN_TOTAL_SAMPLES    = 150
MIN_TEMP_RANGE       = 5.0   # °C
HYSTERESIS_C         = 1.5   # °C either side of boundary
MIN_SLOPE_DROP       = 1.0   # ADC/°C — minimum drop to declare a boundary


# ── HTTP helpers ──────────────────────────────────────────────────────────────
def influx_query(flux: str) -> str:
    req = urllib.request.Request(
        f"{INFLUX_URL}/api/v2/query?orgID={INFLUX_ORG}",
        data=flux.encode(),
        headers={
            "Authorization": f"Token {INFLUX_TOKEN}",
            "Content-Type":  "application/vnd.flux",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read().decode()


def device_rpc(device_url: str, endpoint: str, payload: dict) -> dict:
    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        f"{device_url}/rpc/{endpoint}",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


# ── stats ─────────────────────────────────────────────────────────────────────
def linreg(xs, ys):
    """(slope, intercept, r2, rmse) or None on degenerate input."""
    n = len(xs)
    if n < 2:
        return None
    mx, my = sum(xs) / n, sum(ys) / n
    cov = sum((xs[i] - mx) * (ys[i] - my) for i in range(n)) / n
    vx  = sum((x - mx) ** 2 for x in xs) / n
    if vx == 0:
        return None
    sl  = cov / vx
    ic  = my - sl * mx
    pred  = [sl * x + ic for x in xs]
    ss_r  = sum((ys[i] - pred[i]) ** 2 for i in range(n))
    ss_t  = sum((ys[i] - my) ** 2 for i in range(n))
    r2    = 1.0 - ss_r / ss_t if ss_t else 0.0
    rmse  = (ss_r / n) ** 0.5
    return sl, ic, r2, rmse


# ── data fetch ────────────────────────────────────────────────────────────────
def fetch_data(days: int):
    """Return list of (temperature, adc) tuples aligned by minute timestamp."""
    flux = f'''
from(bucket: "{INFLUX_BUCKET}")
  |> range(start: -{days}d)
  |> filter(fn: (r) => r["_measurement"] == "_tank_raw" or r["_measurement"] == "_tank_status")
  |> filter(fn: (r) => r["_field"] == "tank_pressure_adc" or r["_field"] == "air_temperature")
  |> filter(fn: (r) => r["_value"] != 0.0)
  |> aggregateWindow(every: 1m, fn: mean, createEmpty: false)
  |> yield(name: "mean")
'''
    raw = influx_query(flux)
    adc_map, temp_map = {}, {}
    for line in raw.splitlines():
        parts = line.strip().split(",")
        if len(parts) < 8:
            continue
        try:
            t, val, field = parts[5], float(parts[6]), parts[7]
            if field == "tank_pressure_adc":
                adc_map[t] = val
            elif field == "air_temperature":
                temp_map[t] = val
        except (ValueError, IndexError):
            pass

    times = sorted(set(adc_map) & set(temp_map))
    return times, [(temp_map[t], adc_map[t]) for t in times]


def filter_warming(times, data):
    """Keep only samples where temperature is rising (dT > 0 over the previous minute)."""
    out = []
    for i in range(1, len(data)):
        if data[i][0] > data[i - 1][0]:
            out.append(data[i])
    return out


# ── band analysis ─────────────────────────────────────────────────────────────
def analyze_bands(data, band_size: float, min_r2: float = 0.0):
    """Overlapping bands of width band_size, stride = band_size/2."""
    temps = [d[0] for d in data]
    t_min, t_max = min(temps), max(temps)
    bands = []
    lo = t_min
    while lo + band_size / 2 < t_max:
        hi = min(lo + band_size, t_max)
        pts = [(t, a) for t, a in data if lo <= t < hi]
        if len(pts) >= MIN_SAMPLES_PER_BAND:
            ts = [p[0] for p in pts]
            ас = [p[1] for p in pts]
            fit = linreg(ts, ас)
            if fit and fit[2] >= min_r2:
                sl, _, r2, rmse = fit
                bands.append({
                    "lo": lo, "hi": hi,
                    "mid": (lo + hi) / 2,
                    "slope": sl, "r2": r2, "rmse": rmse,
                    "n": len(pts),
                })
        lo += band_size / 2
    return bands


def find_boundary(bands):
    """Temperature where the largest slope drop between adjacent bands occurs."""
    best_drop, boundary = 0.0, None
    for i in range(1, len(bands)):
        drop = bands[i - 1]["slope"] - bands[i]["slope"]
        if drop > best_drop:
            best_drop = drop
            # Boundary between the midpoints of the two bands
            boundary = (bands[i - 1]["mid"] + bands[i]["mid"]) / 2
    if best_drop < MIN_SLOPE_DROP:
        return None, best_drop
    return boundary, best_drop


def regime_slopes(bands, boundary):
    """Median slope for bands whose midpoint is below / above the boundary."""
    cold = [b["slope"] for b in bands if b["mid"] <  boundary]
    warm = [b["slope"] for b in bands if b["mid"] >= boundary]
    if not cold or not warm:
        return None, None
    return statistics.median(cold), statistics.median(warm)


# ── main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--days",       type=int,   default=7,          help="Days of history (default 7)")
    parser.add_argument("--device",     default=DEVICE_URL,             help="Device base URL")
    parser.add_argument("--band-size",  type=float, default=4.0,        help="Band width in °C (default 4)")
    parser.add_argument("--hysteresis", type=float, default=HYSTERESIS_C, help="±°C around boundary (default 1.5)")
    parser.add_argument("--min-r2",     type=float, default=0.0,        help="Min R² to include a band (default 0)")
    parser.add_argument("--warming-only", action="store_true",
                        help="Use only samples where temperature is rising")
    parser.add_argument("--dry-run",    action="store_true",            help="Analyse only, do not push")
    args = parser.parse_args()

    # ── fetch ──
    print(f"Fetching {args.days}d of data …")
    try:
        times, data = fetch_data(args.days)
    except Exception as e:
        print(f"ERROR fetching data: {e}", file=sys.stderr)
        sys.exit(1)

    if args.warming_only:
        data = filter_warming(times, data)
        print(f"  Warming-only filter: {len(data)} samples retained")

    if len(data) < MIN_TOTAL_SAMPLES:
        print(f"ERROR: only {len(data)} samples (need {MIN_TOTAL_SAMPLES})", file=sys.stderr)
        sys.exit(1)

    temps   = [d[0] for d in data]
    t_swing = max(temps) - min(temps)
    print(f"  {len(data)} samples   T = {min(temps):.1f} – {max(temps):.1f} °C  (swing {t_swing:.1f}°C)")

    if t_swing < MIN_TEMP_RANGE:
        print(f"ERROR: swing {t_swing:.1f}°C < required {MIN_TEMP_RANGE}°C", file=sys.stderr)
        sys.exit(1)

    # ── per-band regression ──
    bands = analyze_bands(data, args.band_size, args.min_r2)
    if not bands:
        print("ERROR: no bands with enough samples", file=sys.stderr)
        sys.exit(1)

    print(f"\nPer-{args.band_size}°C band slopes (min R² filter: {args.min_r2}):")
    for b in bands:
        print(f"  {b['lo']:5.1f}–{b['hi']:5.1f}°C  slope={b['slope']:+.3f} ADC/°C"
              f"  R²={b['r2']:.3f}  n={b['n']}")

    # ── boundary ──
    boundary, drop = find_boundary(bands)

    if boundary is None:
        print(f"\nNo clear boundary (max slope drop = {drop:.2f} < {MIN_SLOPE_DROP} ADC/°C).")
        fit = linreg(temps, [d[1] for d in data])
        if fit:
            sl, _, r2, _ = fit
            print(f"Fallback: global slope = {sl:.4f} ADC/°C  R²={r2:.3f}")
            if not args.dry_run:
                result = device_rpc(args.device, "Calibration.SetSlope", {"slope": round(sl, 4)})
                print(f"Device: {result}")
        return

    t_cold = round(boundary - args.hysteresis, 1)
    t_warm = round(boundary + args.hysteresis, 1)
    slope_cold, slope_warm = regime_slopes(bands, boundary)

    if slope_cold is None:
        print("ERROR: could not split bands into two regimes", file=sys.stderr)
        sys.exit(1)

    # Sanity check: compensated std with new two-regime model
    comp = [adc - (slope_cold if T < boundary else slope_warm) * T
            for T, adc in data]
    std_comp = statistics.stdev(comp)

    print(f"\nResult:")
    print(f"  Boundary:        {boundary:.1f}°C  (slope drop = {drop:.2f} ADC/°C)")
    print(f"  Hysteresis:      warm→cold at {t_cold}°C,  cold→warm at {t_warm}°C")
    print(f"  slope_cold:      {slope_cold:.4f} ADC/°C")
    print(f"  slope_warm:      {slope_warm:.4f} ADC/°C")
    print(f"  Compensated std: {std_comp:.2f} ADC units")

    if args.dry_run:
        print("\n(--dry-run: not pushing to device)")
        return

    payload = {
        "slope_cold": round(slope_cold, 4),
        "slope_warm": round(slope_warm, 4),
        "t_cold":     t_cold,
        "t_warm":     t_warm,
    }
    print(f"\nPushing to {args.device} …  {payload}")
    try:
        result = device_rpc(args.device, "Calibration.SetCoefficients", payload)
        print(f"Device: {result}")
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

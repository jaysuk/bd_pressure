"""
pa_log_plot.py — bd_pressure PA calibration log visualiser

Usage:
    python pa_log_plot.py [logfile.csv]

If no file is given it looks for pa_calibrate_log.txt in the current directory.
"""

import sys
import os
import csv
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec


def load_log(path):
    meta = {}
    rows = []
    with open(path, newline='') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#'):
                # Parse "# key=value" header lines
                content = line.lstrip('#').strip()
                if '=' in content:
                    key, _, val = content.partition('=')
                    meta[key.strip()] = val.strip()
                continue
            # Hand remaining lines to csv.DictReader via a list
            break
        # Re-open to feed remaining non-comment lines through DictReader
    with open(path, newline='') as f:
        non_comment = (l for l in f if not l.startswith('#'))
        reader = csv.DictReader(non_comment)
        for row in reader:
            try:
                rows.append({
                    'iter': int(row['iter']),
                    'pa':   float(row['pa']),
                    'res':  int(row['res']),
                    'lk':   int(row['lk']),
                    'rk':   int(row['rk']),
                    'Hk':   int(row['Hk']),
                    'Ha':   int(row['Ha']),
                })
            except (KeyError, ValueError):
                continue
    return meta, rows


def find_best(rows, skip=5):
    candidates = [r for r in rows if r['iter'] >= skip and r['res'] > 0]
    if not candidates:
        return None
    return min(candidates, key=lambda r: r['res'])


def plot(meta, rows, logpath):
    pa   = [r['pa']  for r in rows]
    res  = [r['res'] for r in rows]
    lk   = [r['lk']  for r in rows]
    rk   = [r['rk']  for r in rows]
    Hk   = [r['Hk']  for r in rows]
    Ha   = [r['Ha']  for r in rows]

    best = find_best(rows)

    fig = plt.figure(figsize=(14, 10))

    # Build subtitle from metadata
    meta_parts = []
    if 'date' in meta:        meta_parts.append(meta['date'])
    if 'rrf_version' in meta: meta_parts.append(f"RRF {meta['rrf_version']}")
    if 'bd_version' in meta:  meta_parts.append(meta['bd_version'])
    if 'mode' in meta:        meta_parts.append(f"mode={meta['mode']}")
    if 'nozzle_temp' in meta: meta_parts.append(f"nozzle {meta['nozzle_temp']}°C")
    if 'pa_start' in meta and 'pa_step' in meta and 'steps' in meta:
        meta_parts.append(f"PA {meta['pa_start']}+{meta['pa_step']}×{meta['steps']}")

    fig.suptitle(f"bd_pressure PA calibration — {os.path.basename(logpath)}", fontsize=13, fontweight='bold')
    if meta_parts:
        fig.text(0.5, 0.94, '   |   '.join(meta_parts), ha='center', fontsize=9, color='#555')

    gs = gridspec.GridSpec(3, 1, hspace=0.50, top=0.90)

    # --- Top: res (primary score, lower = better) ---
    ax1 = fig.add_subplot(gs[0])
    ax1.plot(pa, res, 'b-o', markersize=4, linewidth=1.2, label='res (lower = better)')
    if best:
        ax1.axvline(best['pa'], color='red', linestyle='--', linewidth=1.2, label=f"best PA={best['pa']:.4f} (res={best['res']})")
        ax1.scatter([best['pa']], [best['res']], color='red', zorder=5, s=60)
    ax1.set_ylabel('res', fontsize=11)
    ax1.set_title('Pressure score (res)', fontsize=10)
    ax1.legend(fontsize=9)
    ax1.grid(True, linestyle='--', alpha=0.5)
    ax1.set_xlim(min(pa), max(pa))

    # --- Middle: lk and rk (slopes) ---
    ax2 = fig.add_subplot(gs[1])
    ax2.plot(pa, lk, 'g-o', markersize=3, linewidth=1.0, label='lk (left slope)')
    ax2.plot(pa, rk, 'm-o', markersize=3, linewidth=1.0, label='rk (right slope)')
    if best:
        ax2.axvline(best['pa'], color='red', linestyle='--', linewidth=1.2)
    ax2.set_ylabel('slope', fontsize=11)
    ax2.set_title('Slopes (lk / rk)', fontsize=10)
    ax2.legend(fontsize=9)
    ax2.grid(True, linestyle='--', alpha=0.5)
    ax2.set_xlim(min(pa), max(pa))

    # --- Bottom: Hk and Ha (peak heights — signal quality) ---
    ax3 = fig.add_subplot(gs[2])
    ax3.plot(pa, Hk, 'c-o', markersize=3, linewidth=1.0, label='Hk (left peak)')
    ax3.plot(pa, Ha, 'orange', marker='o', markersize=3, linewidth=1.0, label='Ha (right peak)')
    ax3.axhline(50, color='gray', linestyle=':', linewidth=1.0, label='quality floor (50)')
    if best:
        ax3.axvline(best['pa'], color='red', linestyle='--', linewidth=1.2)
    ax3.set_ylabel('height', fontsize=11)
    ax3.set_xlabel('Pressure Advance', fontsize=11)
    ax3.set_title('Peak heights / signal quality (Hk / Ha)', fontsize=10)
    ax3.legend(fontsize=9)
    ax3.grid(True, linestyle='--', alpha=0.5)
    ax3.set_xlim(min(pa), max(pa))

    if best:
        fig.text(0.5, 0.01,
                 f"Best PA = {best['pa']:.4f}   res={best['res']}   lk={best['lk']}   rk={best['rk']}   Hk={best['Hk']}   Ha={best['Ha']}",
                 ha='center', fontsize=10, color='red', fontweight='bold')

    plt.show()


if __name__ == '__main__':
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        path = 'pa_calibrate_log.txt'

    if not os.path.exists(path):
        print(f"Error: file not found: {path}")
        sys.exit(1)

    meta, rows = load_log(path)

    if meta:
        print("Log metadata:")
        for k, v in meta.items():
            print(f"  {k} = {v}")

    print(f"Loaded {len(rows)} iterations from {path}")
    best = find_best(rows)
    if best:
        print(f"Best PA = {best['pa']:.4f}  (res={best['res']}, lk={best['lk']}, rk={best['rk']}, Hk={best['Hk']}, Ha={best['Ha']})")
    plot(meta, rows, path)

"""Release acceptance matrix. Standard library only; each run gets a fresh process."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("exe", type=Path)
parser.add_argument("--out", type=Path, default=Path("docs/culling/results"))
parser.add_argument("--runs", type=int, default=3)
parser.add_argument("--frames", type=int, default=360)
parser.add_argument("--animated", action="store_true")
args = parser.parse_args()
if args.runs < 3:
    parser.error("Acceptance requires at least three runs")
exe = args.exe.resolve()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
modes = {
    "before": ["--culling-off", "--sorting-off"],
    "sort_only": ["--culling-off"],
    "cull_only": ["--sorting-off"],
    "group_only": ["--depth-sort-off"],
    "after": [],
}

def run(name, flags, frames):
    path = out / (name + ".csv")
    command = [str(exe), "--culling-bench", *flags, "--culling-frames", str(frames),
               "--culling-out", str(path)]
    if args.animated: command.append("--culling-animated")
    with (out / (name + ".log")).open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    if result.returncode:
        raise RuntimeError(f"{name} failed ({result.returncode}); see log")
    text = path.read_text(encoding="utf-8")
    if "# complete=1" not in text:
        raise RuntimeError(f"{name}: incomplete capture")
    rows = list(csv.DictReader(line for line in text.splitlines() if not line.startswith("#")))
    if len(rows) != frames:
        raise RuntimeError(f"{name}: {len(rows)} samples, expected {frames}")
    return rows

run("verification", ["--culling-verify"], 1)
visual = list(csv.DictReader((out / "visual-check.csv").open()))
if len(visual) != 36 or any(int(row["max_error"]) > 2 for row in visual):
    raise RuntimeError("Image equivalence failed")
print("36 framebuffer comparisons passed", flush=True)
results = {mode: [] for mode in modes}
for repeat in range(args.runs):
    # Rotate execution order to reduce systematic warm/thermal ordering bias.
    order = list(modes)
    order = order[repeat % len(order):] + order[:repeat % len(order)]
    for mode in order:
        rows = run(f"{mode}-{repeat+1}", modes[mode], args.frames)
        for row in rows:
            if int(row["registered"]) != 10000 or int(row["updates"]) != (100 if args.animated else 0):
                raise RuntimeError(f"{mode}: unexpected scene size or static-scene rescan")
        results[mode].append(rows)
        print(f"{mode} {repeat+1}/{args.runs} complete", flush=True)

def stats(values):
    values = sorted(values)
    def percentile(q):
        # Nearest-rank, explicitly reproducible on small samples.
        return values[max(0, math.ceil(q * len(values)) - 1)]
    return {"median": statistics.median(values), "p95": percentile(.95), "p99": percentile(.99)}

summary = {"animated": 100 if args.animated else 0, "runs": args.runs, "frames_per_run": args.frames, "warmup_per_run": 120,
           "visual_comparisons": len(visual), "modes": {}}
for mode, runs in results.items():
    rows = [row for run_rows in runs for row in run_rows]
    entry = {}
    for field in ("frame_ms", "prepare_ms", "submit_ms", "submitted", "texture_changes", "depth_inversions"):
        entry[field] = stats([float(row[field]) for row in rows])
    entry["cpu_render_ms"] = stats([float(row["prepare_ms"]) + float(row["submit_ms"]) for row in rows])
    entry["per_run_frame_ms"] = [stats([float(row["frame_ms"]) for row in rr]) for rr in runs]
    summary["modes"][mode] = entry
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, indent=2), flush=True)

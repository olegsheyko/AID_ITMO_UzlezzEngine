#!/usr/bin/env python3
"""Сводка замеров ЛР 1 по CSV из режима GameEngine --bench.

    python3 tools/bench_stats.py build/mac/bench/before/*.csv build/mac/bench/after/*.csv

Метка прогона — имя папки с CSV (before, after, ...). По каждой метке и сценарию
метрики считаются для каждого прогона отдельно, в таблицу идёт медиана по прогонам.

Окна:
  простой  — кадры до запуска пачки (phase=idle);
  загрузка — от запуска пачки до её готовности плюс хвост (phase=load и tail).
Перцентили — линейная интерполяция по отсортированным кадрам, как numpy.percentile.
"""

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

HITCH_MS = 33.3  # дольше двух кадров при 60 Гц — считаем фризом


def percentile(sorted_values, q):
    if not sorted_values:
        return float("nan")
    pos = (len(sorted_values) - 1) * q / 100.0
    lo = int(pos)
    hi = min(lo + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (pos - lo)


def read_run(path):
    meta, data = {}, []
    for line in Path(path).read_text().splitlines():
        if line.startswith("#"):
            key, sep, value = line[1:].strip().partition("=")
            if sep:
                meta[key.strip()] = value.strip()
        elif line:
            data.append(line)
    rows = [(r["phase"], float(r["frame_ms"]), float(r["main_load_ms"])) for r in csv.DictReader(data)]
    return meta, rows


def run_metrics(rows):
    idle = sorted(ms for phase, ms, _ in rows if phase == "idle")
    window = [(ms, main) for phase, ms, main in rows if phase in ("load", "tail")]
    frames = sorted(ms for ms, _ in window)
    return {
        "idle_n": len(idle),
        "idle_p50": percentile(idle, 50),
        "idle_p95": percentile(idle, 95),
        "idle_p99": percentile(idle, 99),
        "win_n": len(frames),
        "win_p50": percentile(frames, 50),
        "win_p95": percentile(frames, 95),
        "win_p99": percentile(frames, 99),
        "win_max": frames[-1] if frames else float("nan"),
        "hitches": sum(1 for ms in frames if ms > HITCH_MS),
        "main_load": sum(main for _, main in window),
    }


def spread(values, fmt="{:.1f}"):
    """Медиана по прогонам и диапазон, если прогонов больше одного."""
    mid = fmt.format(statistics.median(values))
    if len(values) < 2:
        return mid
    return f"{mid} ({fmt.format(min(values))}–{fmt.format(max(values))})"


def main(paths):
    if not paths:
        print(__doc__.strip())
        return 2

    groups = defaultdict(list)
    warnings = []
    for path in paths:
        meta, rows = read_run(path)
        if meta.get("exit_during_load") == "1":
            continue  # проверка шатдауна, а не замер — в таблицу не идёт
        label = Path(path).parent.name
        key = (label, meta.get("scenario", "?"), meta.get("loading", "?"))
        metrics = run_metrics(rows)
        metrics["batch_ms"] = float(meta.get("batch_ms", "nan"))
        groups[key].append(metrics)
        if meta.get("vsync") != "0":
            warnings.append(f"{path}: vsync не выключен — время кадра привязано к частоте экрана")
        if meta.get("complete") != "1":
            warnings.append(f"{path}: пачка не догрузилась до таймаута")
        if meta.get("batch_failed", "0") != "0":
            warnings.append(f"{path}: не загрузилось текстур: {meta['batch_failed']}")

    for (label, scenario, _), runs in groups.items():
        if len(runs) < 3:
            warnings.append(f"{label}/{scenario}: прогонов {len(runs)}, по методике нужно не меньше 3")

    ordered = sorted(groups.items(), key=lambda item: (item[0][1], item[0][0]))

    print("### Время кадра, мс — медиана по прогонам\n")
    print("| Прогон | Загрузка | Сценарий | Простой p50 | p95 | p99 | Окно загрузки p50 | p95 | p99 | Худший кадр |")
    print("|---|---|---|---:|---:|---:|---:|---:|---:|---:|")
    for (label, scenario, loading), runs in ordered:
        med = lambda k: statistics.median(r[k] for r in runs)
        print(f"| {label} | {loading} | {scenario} "
              f"| {med('idle_p50'):.2f} | {med('idle_p95'):.2f} | {med('idle_p99'):.2f} "
              f"| {med('win_p50'):.2f} | {med('win_p95'):.2f} | {med('win_p99'):.2f} "
              f"| {spread([r['win_max'] for r in runs])} |")

    print("\n### Загрузка пачки — медиана по прогонам\n")
    print(f"| Прогон | Загрузка | Сценарий | Кадров > {HITCH_MS:g} мс | Главный поток в загрузке, мс | Пачка готова, мс | Кадров в окне | Прогонов |")
    print("|---|---|---|---:|---:|---:|---:|---:|")
    for (label, scenario, loading), runs in ordered:
        print(f"| {label} | {loading} | {scenario} "
              f"| {spread([r['hitches'] for r in runs], '{:.0f}')} "
              f"| {spread([r['main_load'] for r in runs])} "
              f"| {spread([r['batch_ms'] for r in runs])} "
              f"| {statistics.median(r['win_n'] for r in runs):.0f} | {len(runs)} |")

    if warnings:
        print("\n**Предупреждения:**\n")
        for warning in warnings:
            print(f"- {warning}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

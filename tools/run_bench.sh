#!/usr/bin/env bash
# Замеры ЛР 1 на macOS: собирает Release, гоняет оба сценария N раз, печатает сводку.
#
#   tools/run_bench.sh <метка> [прогонов=3] [async|sync, по умолчанию async]
#
# CSV складываются в build/mac/bench/<метка>/<сценарий>_<i>.csv.
# Во время прогонов не сворачивай и не перекрывай окно движка: macOS притормаживает
# невидимые окна, и цифры поплывут.
set -euo pipefail

label="${1:?укажи метку прогона, например: tools/run_bench.sh before}"
runs="${2:-3}"
mode="${3:-async}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/mac"
out="$build/bench/$label"

[ -f "$build/CMakeCache.txt" ] || cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target GameEngine CopyAssets --parallel "$(sysctl -n hw.ncpu)" > /dev/null

rm -rf "$out"
mkdir -p "$out"
for scenario in burst stream; do
    for i in $(seq 1 "$runs"); do
        echo "[$label] $scenario, $mode: прогон $i из $runs"
        # perl alarm вместо timeout — в macOS его нет; зависший прогон убьётся через 2 минуты.
        (cd "$build" && perl -e 'alarm shift; exec @ARGV' 120 \
            ./GameEngine --bench "$scenario" --load-mode "$mode" --bench-out "bench/$label/${scenario}_$i.csv" > /dev/null 2>&1) \
            || { echo "прогон упал или завис, смотри $build/engine.log" >&2; exit 1; }
    done
done

echo
python3 "$root/tools/bench_stats.py" "$out"/*.csv

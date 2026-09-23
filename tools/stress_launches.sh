#!/usr/bin/env bash
# Многократные запуски для блока стабильности ЛР 1 на macOS: N запусков подряд, каждый
# выходит в случайный момент — посреди декодирования, заливки, выгрузки или простоя.
# Каждый пятый запуск выходит ровно посреди загрузки пачки (--exit-during-load).
#
#   tools/stress_launches.sh [запусков=50]
#
# Провал — ненулевой код, зависание дольше минуты или выход без «Shutdown complete» в логе.
set -euo pipefail

runs="${1:-50}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/mac"
out="$build/bench/launches"

[ -f "$build/CMakeCache.txt" ] || cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target GameEngine CopyAssets --parallel "$(sysctl -n hw.ncpu)" > /dev/null

rm -rf "$out"
mkdir -p "$out"
touch "$build/engine.log"
ok=0; crashed=0; hung=0; unclean=0; live=0

for i in $(seq 1 "$runs"); do
    if (( i % 5 == 0 )); then
        args=(--bench burst --load-mode async --exit-during-load --bench-out "bench/launches/exit_$i.csv")
        what="exit during load"
    else
        # 1,5–6 с: выход попадает в разные фазы цикла стресса.
        seconds=$(awk -v seed="$RANDOM$i" 'BEGIN { srand(seed); printf "%.2f", 1.5 + rand() * 4.5 }')
        args=(--stress-seconds "$seconds")
        what="stress ${seconds} s"
    fi

    before=$(wc -l < "$build/engine.log")
    set +e
    # perl alarm вместо timeout — в macOS его нет; код 142 значит, что процесс завис и убит.
    (cd "$build" && perl -e 'alarm shift; exec @ARGV' 60 ./GameEngine "${args[@]}" > /dev/null 2>&1)
    code=$?
    set -e
    tail -n +$((before + 1)) "$build/engine.log" > "$out/run_$i.log"

    pending=$(grep -o "shutting down, [0-9]* loads pending" "$out/run_$i.log" | grep -o "[0-9]*" | head -1 || true)
    [ "${pending:-0}" -gt 0 ] && live=$((live + 1))

    if [ $code -eq 142 ]; then
        hung=$((hung + 1)); status="ЗАВИС"
    elif [ $code -ne 0 ]; then
        crashed=$((crashed + 1)); status="УПАЛ, код $code"
    elif ! grep -q "Shutdown complete" "$out/run_$i.log"; then
        unclean=$((unclean + 1)); status="НЕЧИСТЫЙ ВЫХОД"
    else
        ok=$((ok + 1)); status="ok"
    fi
    printf "[%2d/%d] %-18s загрузок в полёте при выходе: %-3s %s\n" "$i" "$runs" "$what" "${pending:-?}" "$status"
done

echo
echo "итого: чистых выходов $ok из $runs; упало $crashed, зависло $hung, нечистый выход $unclean"
echo "выходов с живыми загрузками: $live из $runs; логи — $out"
[ "$ok" -eq "$runs" ]

#!/bin/bash
# benchmark_threads.sh — 不同 SubReactor 线程数对比测试（探索性，非基线）
# 用途：观察「服务器 -t vs QPS」的趋势，尤其「超过核数反而掉 QPS」。
# 前置：在 bin/ 下执行；会自己启动/停止服务器（./Server -p 7070 -t $t），
#       先确认没有别的 ./Server 在跑。
# 口径：与 §9 对齐——补 keep-alive、wrk -t4、ulimit -n 65535、127.0.0.1。
# 注意：THREADS 里刻意包含 12（8 核上属超订）。这是本脚本唯一的价值——
#       让你亲眼看到超订掉 QPS，但 12 那一行永远不能写进性能表。

PORT=7070
DURATION=20
THREADS=(1 2 4 8 12)

ulimit -n 65535

if pgrep -f './Server' >/dev/null 2>&1; then
    echo "错误：检测到已有 ./Server 在跑，先停掉再执行。" >&2
    exit 1
fi

echo "======= 线程数 vs QPS 对比测试 ======="
echo ""

for t in "${THREADS[@]}"; do
    echo ">>> 测试 ${t} 线程..."

    # 启动服务器
    ./Server -p $PORT -t $t &
    SERVER_PID=$!
    sleep 2

    # 压测
    echo "--- wrk: 4线程 x 500连接 x ${DURATION}s ---"
    wrk -t4 -c500 -d${DURATION}s -H "Connection: keep-alive" --latency http://127.0.0.1:$PORT/

    # 停止服务器
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    sleep 1
    echo ""
done

echo "======= 测试完成 ======="

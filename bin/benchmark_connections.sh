#!/bin/bash
# benchmark_connections.sh — 并发连接数梯度测试（探索性，非基线）
# 用途：观察「并发连接数 vs QPS」的趋势。数字不可与 README.md「性能参考」的四个基线场景混比。
# 前置：在 bin/ 下执行；会自己启动/停止服务器（./Server -p 7070 -t 6），
#       所以先确认没有别的 ./Server 在跑，否则撞 7070 端口。
# 口径：与 §9 对齐——补 keep-alive、wrk -t4、服务器 -t6、ulimit -n 65535、127.0.0.1。

PORT=7070
DURATION=20
CONNS=(100 500 1000 2000)

ulimit -n 65535

# 端口占用 fail-fast：先确认没有别的 ./Server 在跑，否则下面的 bind 会失败
if pgrep -f './Server' >/dev/null 2>&1; then
    echo "错误：检测到已有 ./Server 在跑，先停掉再执行（本脚本会自己启停服务器）。" >&2
    exit 1
fi

# 启动服务器（-t 6 ≈ 核数 × 0.75，见 README.md「运行」）
./Server -p $PORT -t 6 &
SERVER_PID=$!
sleep 2

echo "======= 并发连接数 vs QPS 对比测试 ======="
echo ""

for c in "${CONNS[@]}"; do
    echo ">>> wrk: 4线程 x ${c}连接 x ${DURATION}s"
    wrk -t4 -c${c} -d${DURATION}s -H "Connection: keep-alive" --latency http://127.0.0.1:$PORT/
    echo ""
done

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
echo "======= 测试完成 ======="

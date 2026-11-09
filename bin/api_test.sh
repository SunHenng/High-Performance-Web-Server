#!/bin/bash
# ============================================================
# ModernWebServer 接口回归测试
#
# 用法（必须在 bin/ 目录下执行，脚本会拉起 ./Server）：
#     cd ModernWebServer/bin
#     ./api_test.sh            # 默认端口 7071
#     ./api_test.sh 8080       # 指定端口
#
# 前置条件：
#   - MySQL 已启动，users 表已按 README.md「初始化数据库」建好
#   - 已编译出 bin/Server（见 README.md「编译」）
#   - 不要求端口空闲以外的任何环境准备，脚本自己启停服务器
#
# 说明：脚本默认用 7071 而不是 7070，避免和已经跑着的实例抢端口。
#       全部用例通过时退出码为 0，否则为 1。
# ============================================================

PORT="${1:-7071}"
# 用 127.0.0.1 而非 localhost：服务器只监听 IPv4（AF_INET），localhost 会先解析到
# ::1 再回落，就绪检查的 --max-time 1 可能被这次回落拖到超时。
BASE="http://127.0.0.1:${PORT}"
SERVER="./Server"
SERVER_LOG="/tmp/mws_api_test_server.log"
# 测试账号每次运行唯一（时间戳 + PID），保证「注册成功」这一步可严格断言为
# "success": true，且脚本可反复重跑而无需人工清理。密码固定即可（账号是新的）。
# 累积：每次运行会在 users 表留一个新用户，如需清理：
#   mysql -uroot -p<db_password> testdb -e "DELETE FROM users WHERE username LIKE 'apitest_user\_%';"
TEST_USER="apitest_user_$(date +%s)_$$"
TEST_PASS="apitest_pass_$(date +%s)_$$"

PASS=0
FAIL=0
SERVER_PID=""

# ---------- 输出helpers ----------
pass() { PASS=$((PASS + 1)); printf '  \033[32m[PASS]\033[0m %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  \033[31m[FAIL]\033[0m %s\n' "$1"; }

check_eq() { # 期望值 实际值 说明
    if [ "$1" = "$2" ]; then
        pass "$3"
    else
        fail "$3"
        printf '         期望: %s\n         实际: %s\n' "$1" "$2"
    fi
}

check_contains() { # 全文 子串 说明
    case "$1" in
    *"$2"*) pass "$3" ;;
    *)
        fail "$3"
        printf '         期望包含: %s\n         实际: %.200s\n' "$2" "$1"
        ;;
    esac
}

http_code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

# ---------- 启动服务器 ----------
if [ ! -x "$SERVER" ]; then
    echo "找不到可执行文件 $SERVER"
    echo "请先执行： cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build --parallel \$(nproc)"
    exit 1
fi

echo "启动服务器（端口 $PORT，日志 $SERVER_LOG）..."
"$SERVER" -p "$PORT" -t 4 >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

READY=0
for _ in $(seq 1 60); do
    if curl -s -o /dev/null --max-time 1 "$BASE/"; then
        READY=1
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "服务器进程已退出，日志尾部："
        tail -n 20 "$SERVER_LOG"
        exit 1
    fi
    sleep 0.2
done

if [ "$READY" -ne 1 ]; then
    echo "服务器 12 秒内未就绪，日志尾部："
    tail -n 20 "$SERVER_LOG"
    exit 1
fi
echo "服务器就绪。"

echo ""
echo "========================================"
echo "1. 静态文件服务"
echo "========================================"
check_eq "200" "$(http_code "$BASE/")" "GET / 返回 200"
check_eq "200" "$(http_code "$BASE/index.html")" "GET /index.html 返回 200"
check_eq "200" "$(http_code "$BASE/login.html")" "GET /login.html 返回 200"
check_eq "404" "$(http_code "$BASE/nonexistent.html")" "GET /nonexistent.html 返回 404"

echo ""
echo "========================================"
echo "2. 目录穿越防护"
echo "========================================"
# static/ 的上级目录里放着 README.md，是现成的越界目标
check_eq "404" "$(http_code --path-as-is "$BASE/../README.md")" \
    "/../README.md 被拒绝（404）"
check_eq "404" "$(http_code --path-as-is "$BASE/../../../../etc/passwd")" \
    "/../../../../etc/passwd 被拒绝（404）"

TRAVERSE_BODY=$(curl -s --path-as-is "$BASE/../../../../etc/passwd")
check_contains "$TRAVERSE_BODY" "404 Not Found" "越界请求不泄漏文件内容"

echo ""
echo "========================================"
echo "3. 注册 / 登录接口"
echo "========================================"
REG=$(curl -s -X POST "$BASE/register" -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")
# 必须严格断言 "success": true——只查 '"success"' 会把重复注册的 false 也放过去。
# 账号每次运行唯一，所以这里的第一次注册必然成功。
check_contains "$REG" '"success": true' "POST /register 注册成功（新用户）"

LOGIN=$(curl -s -X POST "$BASE/login" -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")
check_contains "$LOGIN" '"success": true' "POST /login 正确密码登录成功"

BAD=$(curl -s -X POST "$BASE/login" -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"definitely-wrong\"}")
check_contains "$BAD" '"success": false' "POST /login 错误密码登录失败"

DUP=$(curl -s -X POST "$BASE/register" -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")
check_contains "$DUP" '"success": false' "重复注册同名用户被拒绝"

echo ""
echo "========================================"
echo "4. 状态行文案（回归：401 曾错报为 Internal Server Error）"
echo "========================================"
STATUS_LINE=$(curl -s -D - -o /dev/null -X POST "$BASE/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"definitely-wrong\"}" |
    head -1 | tr -d '\r')
check_eq "HTTP/1.1 401 Unauthorized" "$STATUS_LINE" "登录失败返回 401 Unauthorized"

STATUS_LINE=$(curl -s -D - -o /dev/null -X POST "$BASE/login" \
    -H "Content-Type: application/json" -d '{}' | head -1 | tr -d '\r')
check_eq "HTTP/1.1 400 Bad Request" "$STATUS_LINE" "参数缺失返回 400 Bad Request"

echo ""
echo "========================================"
echo "5. 响应定界（回归：认证接口曾不带 Content-Length）"
echo "========================================"
HDRS=$(curl -s -D - -o /dev/null -X POST "$BASE/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")
check_contains "$HDRS" "Content-Length:" "登录响应带 Content-Length"

# 关键回归：keep-alive 下若响应没有 Content-Length，
# 客户端无法判断消息边界，只能一直读到连接关闭（默认要等 30s 空闲超时）。
# 这里限时 5 秒，被 --max-time 掐断即说明定界有问题。
KA_BODY=$(curl -s --max-time 5 -H "Connection: keep-alive" -X POST "$BASE/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}")
KA_RC=$?
check_eq "0" "$KA_RC" "keep-alive 下 5 秒内返回（未挂起等待连接关闭）"
check_contains "$KA_BODY" '"success"' "keep-alive 响应体读取完整"

# 浏览器发送的就是 Connection: keep-alive，对应 login.html 的 fetch 调用
KA_STATIC=$(curl -s --max-time 5 -H "Connection: keep-alive" "$BASE/")
check_contains "$KA_STATIC" "<html" "静态文件在 keep-alive 下同样正常定界"

# 关键回归（连接复用）：同一条 keep-alive 连接上「先 POST 带请求体、再 GET 不带请求体」。
#
# 上面两条 keep-alive 用例各调用一次 curl，那是**两条独立连接**，因此碰不到本缺陷。
# conn->request 是整条连接共用的同一个对象：上一个请求的 Content-Length 会残留，
# 使紧随其后的无请求体 GET 停在 BODY 状态，永远等不到响应、服务端此后再不回应。
#
# 这正是浏览器跳转挂死的路径——register.html 提交 POST /register 成功后，
# 1 秒后跳转 GET /login.html，走的是同一条连接。故浏览器必现而 curl 测不出。
#
# 用「一次 curl 传两个 URL」（curl 会复用连接）来还原它。--next 用于重置选项，
# 保证第二个请求是**不带请求体**的 GET；少了 --next，-d 会作用到两个 URL，
# 两个请求都带 Content-Length，缺陷又会被掩盖。
KA_SEQ=$(curl -s --max-time 5 -o /dev/null -w '%{http_code} ' \
    -H "Connection: keep-alive" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}" "$BASE/login" \
    --next -s --max-time 5 -o /dev/null -w '%{http_code}' \
    -H "Connection: keep-alive" "$BASE/index.html" 2>/dev/null)
KA_SEQ_RC=$?
check_eq "0" "$KA_SEQ_RC" "同连接 POST→GET 未挂起（连接复用回归）"
check_eq "200 200" "$KA_SEQ" "同连接上两个请求都正常返回"

# 上一条用例只有在 curl 真的复用了连接时才有意义。若将来 curl 改了行为、使它退化成
# 两条连接，这条断言会把「用例已失效」暴露出来，而不是让缺陷悄悄溜过去——
# 本缺陷能潜伏这么久，正是因为原有用例每次都在换新连接。
KA_REUSE=$(curl -v --max-time 5 -o /dev/null -H "Connection: keep-alive" \
    -d "{\"username\":\"$TEST_USER\",\"password\":\"$TEST_PASS\"}" "$BASE/login" \
    --next -v --max-time 5 -o /dev/null -H "Connection: keep-alive" \
    "$BASE/index.html" 2>&1 | grep -c "Re-using existing connection" || true)
check_eq "1" "$KA_REUSE" "curl 确实复用了同一条连接（上条用例成立的前提）"

echo ""
echo "========================================"
echo "6. 请求体格式容忍度"
echo "========================================"
SPACED=$(curl -s -X POST "$BASE/login" -H "Content-Type: application/json" \
    -d "{ \"username\" : \"$TEST_USER\" , \"password\" : \"$TEST_PASS\" }")
check_contains "$SPACED" '"success": true' "冒号/逗号前后带空格的 JSON 也能解析"

echo ""
echo "========================================"
echo "7. CORS 预检"
echo "========================================"
OPT_HDRS=$(curl -s -D - -o /dev/null -X OPTIONS "$BASE/login" \
    -H "Origin: http://example.com" \
    -H "Access-Control-Request-Method: POST")
check_contains "$OPT_HDRS" "200 OK" "OPTIONS /login 返回 200"
check_contains "$OPT_HDRS" "Access-Control-Allow-Origin" "OPTIONS /login 带 CORS 头"
check_contains "$OPT_HDRS" "Content-Length:" "OPTIONS 响应带 Content-Length"

echo ""
echo "========================================"
echo "8. 异常输入"
echo "========================================"
check_eq "400" "$(http_code -X POST "$BASE/login" -H 'Content-Type: application/json' -d '{}')" \
    "空 JSON 返回 400"
check_eq "400" "$(http_code -X POST "$BASE/login" -H 'Content-Type: application/json' -d 'not-json')" \
    "非 JSON 请求体返回 400"
check_eq "400" "$(http_code -X POST "$BASE/login" -H 'Content-Type: application/json' -d '{"username":"x"}')" \
    "缺少 password 字段返回 400"

# IsIoTask() 只把 POST /login|/register 归为 CPU 任务，其余一律走静态文件分支，
# 因此未知 POST 路径拿到的是静态文件 404，而不是 ProcessCpuTask 里的兜底响应体。
check_eq "404" "$(http_code -X POST "$BASE/does-not-exist" -d 'x')" \
    "未知 POST 路径返回 404"

echo ""
echo "========================================"
echo "9. 服务器存活检查"
echo "========================================"
if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "全部用例执行完毕后服务器进程仍存活"
else
    fail "服务器进程意外退出"
    tail -n 20 "$SERVER_LOG"
fi

echo ""
echo "========================================"
printf '通过 %d 项，失败 %d 项\n' "$PASS" "$FAIL"
echo "服务器日志：$SERVER_LOG"
echo "========================================"

[ "$FAIL" -eq 0 ]

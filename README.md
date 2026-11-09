# ModernWebServer

基于 C++20 从零实现的 Linux HTTP 服务器：多 Reactor + epoll + 工作窃取线程池 + MySQL 连接池 + 双缓冲日志。

> 仅支持 Linux（依赖 `epoll`、`accept4`）。这是一个面试/学习向项目，不是生产级服务器。

---

## 核心功能

| 模块 | 说明 |
|---|---|
| 多 Reactor 网络层 | 主线程 `accept4` 接收连接，负载均衡器轮询分发，N 个子 Reactor 各自运行 `epoll_wait` |
| HTTP/1.1 协议栈 | 自研解析状态机，支持 9 种方法识别、`Content-Length` 定界、Keep-Alive、静态文件、CORS 预检 |
| 请求分流 | 静态请求在事件循环内直接处理；登录、注册及其 CORS 预检投递 CPU 线程池 |
| 工作窃取线程池 | 每线程独立任务队列，空闲时整队列窃取；C++20 线程对象 + 停止令牌管理退出 |
| MySQL 连接池 | 单例 + RAII；预建 5、按需扩到 50、空闲不缩容；预处理语句防 SQL 注入 |
| 用户认证 | 注册 / 登录，密码经 SHA-256 哈希后落库 |
| 日志 | 双缓冲、前端加锁入队、后台批量刷盘，六级日志，按大小滚动 |
| 空闲连接回收 | EventLoop 定时器每 5 秒扫描，超过 30 秒空闲关闭 |

### 明确没做

- 没有 TLS、HTTP/2、chunked、`100-continue`；
- 静态文件整文件读入，大文件会阻塞事件循环；
- 优雅关闭未接完，`SIGINT` 仍走 `exit(0)`；
- 最大连接数上限路径存在 LT 忙循环缺陷；
- 没有 sanitizer / fuzz / 10 万连接测试。

---

## 环境要求

- Linux（Ubuntu 24.04 已验证）
- g++（支持 C++20）、CMake、make、pkg-config
- MySQL 8.0 及 `libmysqlclient-dev`
- OpenSSL 3.0 及 `libssl-dev`
- 可选：`curl`、`wrk`、`gdb`

```bash
sudo apt update
sudo apt install -y g++ cmake make pkg-config libmysqlclient-dev libssl-dev mysql-server curl
# 可选
sudo apt install -y wrk gdb
```

---

## 1. 初始化数据库

以下示例使用占位符 `<db_password>`，请替换成本机密码，不要把真实密码提交到仓库。

```bash
sudo mysql
```

```sql
CREATE DATABASE IF NOT EXISTS testdb CHARACTER SET utf8mb4;
USE testdb;

CREATE TABLE IF NOT EXISTS users (
    id         INT AUTO_INCREMENT PRIMARY KEY,
    username   VARCHAR(50) UNIQUE NOT NULL,
    password   VARCHAR(64) NOT NULL COMMENT 'SHA256 哈希',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

ALTER USER 'root'@'localhost' IDENTIFIED BY '<db_password>';
EXIT;
```

设置数据库密码环境变量（当前 shell 有效）：

```bash
export MWS_DB_PASSWORD='<db_password>'
export MWS_TEST_PASSWORD='<test_password>'
```

`MWS_DB_PASSWORD` 用于服务器连接数据库；`MWS_TEST_PASSWORD` 只用于 `testMysqlPool` 和 wrk 测试账号。程序未设置 `MWS_DB_PASSWORD` 时使用占位符 `change_me`，不会连接成功。

---

## 2. 编译

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

产物固定输出到 `bin/`，包括 `bin/Server` 和测试程序。换构建目录也会覆盖 `bin/`，切换配置时先清理。

---

## 3. 运行

```bash
export MWS_DB_PASSWORD='<db_password>'
./bin/Server -p 7070 -t 6
```

浏览器打开：

```text
http://127.0.0.1:7070/
```

常用参数：

| 参数 | 含义 | 默认值 |
|---|---|---|
| `-p` | HTTP 端口 | `7070` |
| `-t` | 子 Reactor 数量 | CPU 核心数 |
| `-r` | 静态文件根目录 | 从可执行文件路径推导 `static/` |
| `-P` | 数据库密码 | `MWS_DB_PASSWORD` 或 `change_me` |
| `-i` | 空闲连接超时秒数 | `30` |
| `-M` | 最大并发连接数 | `10000` |

> 必须在项目根目录执行 `./bin/Server`，静态目录按可执行文件路径上跳两级推导；移动二进制后需用 `-r` 指定。

---

## 4. 测试

### 单元测试

```bash
(cd build && ctest --output-on-failure)
```

当前 `testHttpParser`：13 个用例 / 90 项断言，覆盖请求行、9 种方法、分片、请求体、响应长度、Keep-Alive 对象复用等。

### 接口回归

```bash
(cd bin && bash api_test.sh)
```

脚本会自己在 7071 端口启停服务器，覆盖静态文件、目录穿越、注册登录、状态行、同连接 POST→GET、CORS 预检等 9 组 / 29 项断言。

### 压测（可选）

```bash
ulimit -n 65535
wrk -t4 -c1000 -d30s -H "Connection: keep-alive" http://127.0.0.1:7070/
```

服务器只在客户端显式发送 `Connection: keep-alive` 时复用连接；不加该头测出来的是短连接。

---

## 性能参考

2026-10-04 在 Ubuntu 24.04、8 vCPU VMware、8GB 内存上实测：

| 场景 | 并发 | 服务端 | QPS | 平均延迟 |
|---|---|---:|---:|---:|
| 静态文件 Keep-Alive | 4t×1000c | `-t 6 -i 10` | **132,350** | 7.37ms |
| 静态文件 短连接 | 4t×500c | `-t 6 -i 10` | 91,800 | 4.20ms |
| 注册接口 | 2t×200c | `-t 8 -i 10` | 4,900 | 40.8ms |
| 登录接口 | 2t×200c | `-t 8 -i 10` | 8,439 | 23.5ms |

口径：静态场景连跑 3 次、三次全计取中位数；注册/登录连跑 4 次、丢弃第 1 次、记后 3 次中位数。单机自压（wrk 与 Server 抢同一个 8 vCPU），小文件、page cache 命中，不代表生产上限。

---

## 项目结构

```text
src/          核心源码
include/      头文件
example/      Server 入口与测试程序
static/       静态页面
bin/          测试 / 压测脚本
CMakeLists.txt
README.md
LICENSE
.gitignore
```

---

## 已知问题

- Keep-Alive 复用必须同时 Reset parser / request / response；漏掉 request 会让残留 `Content-Length` 导致后续 GET 永久挂起。
- 日志对象析构时每行调用 `flush()`，实际会退化成同步刷盘。
- 连接池 ping 失败重建时计数会虚高；借连接时在锁内建连，最长阻塞 3 秒。
- 达到最大连接数时监听 fd 仍可读，LT 下会忙循环。
- 注册是“先查重再插入”，并发下依赖唯一索引兜底。

---

## 技术栈

`C++20` · `CMake` · `epoll` · `pthread` · `MySQL 8.0` · `OpenSSL 3.0`

## 许可证

Apache License 2.0

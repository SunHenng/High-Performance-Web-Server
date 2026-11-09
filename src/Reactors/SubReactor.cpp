#include "Reactors/SubReactor.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>
#include "log/Logger.hpp"
#include "db/mysql_connectionPool.hpp"

namespace Reactors
{
    namespace
    {
        constexpr size_t kMaxRequestBytes = 1024 * 1024;
    }

    SubReactor::SubReactor(const std::string &root_dir, int idle_timeout_seconds)
        : sub_loop_(),
          connections_(),
          pending_fds_(),
          queue_mutex_(),
          cpu_thread_pool_(std::make_shared<WorkStealingPool>(200, 2)), // 每 SubReactor 2 CPU 线程，避免过度订阅
          root_dir_(root_dir),
          idle_timeout_seconds_(idle_timeout_seconds)
    {
    }

    SubReactor::~SubReactor()
    {
        Stop();
    }

    // 启动子Reactor的事件循环
    void SubReactor::Start()
    {
        // 注册空闲连接检查定时器（每5秒检查一次）
        idle_check_timer_id_ = sub_loop_.RunEvery(5000, [this]()
                                                  { CheckIdleConnections(); });

        LOG_INFO << "SubReactor idle check timer registered (interval=5s, timeout="
                 << idle_timeout_seconds_ << "s)";

        sub_loop_.Run();
    }

    // 停止子Reactor的事件循环
    void SubReactor::Stop()
    {
        if (idle_check_timer_id_ >= 0)
        {
            sub_loop_.CancelTimer(idle_check_timer_id_);
            idle_check_timer_id_ = -1;
        }
        sub_loop_.Stop();
        cpu_thread_pool_->Stop();
    }

    size_t SubReactor::GetConnectionCount() const
    {
        return connection_count_.load(std::memory_order_acquire);
    }

    void SubReactor::AddConnection(int client_fd, const sockaddr_in &addr)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_fds_.emplace(client_fd, addr);
        }

        sub_loop_.QueueInLoop([this]()
                              { HandlePendingConnections(); });
    }

    // 处理待处理的连接
    void SubReactor::HandlePendingConnections()
    {
        std::queue<std::pair<int, sockaddr_in>> pending_fds;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_fds.swap(pending_fds_);
        }

        while (!pending_fds.empty())
        {
            auto [fd, addr] = pending_fds.front();
            pending_fds.pop();
            CreateConnection(fd, addr);
        }
    }

    // 为新连接创建处理逻辑
    void SubReactor::CreateConnection(int fd, const sockaddr_in &addr)
    {
        auto conn = std::make_shared<TcpConnection>(fd, addr);

        // 重置解析器状态
        conn->parser.Reset();
        conn->read_buffer.clear();
        sub_loop_.UpdateChannel(fd, EventLoop::EventType::kReadEvent, std::bind(&SubReactor::OnRead, this, conn));

        connections_[fd] = conn;
        connection_count_.fetch_add(1, std::memory_order_release);
    }

    // 读事件处理
    void SubReactor::OnRead(TcpConnectionPtr conn)
    {
        // 更新最后活动时间
        conn->last_activity = std::chrono::steady_clock::now();

        char buff[4096];
        int nread = read(conn->fd, buff, sizeof(buff));

        if (nread < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                CloseConnection(conn);
                return;
            }
        }
        else if (nread == 0)
        {
            CloseConnection(conn);
            return;
        }

        // 将新读取的数据追加到缓冲区
        if (nread > 0)
        {
            conn->read_buffer.append(buff, static_cast<size_t>(nread));
        }
        if (nread < 0 && conn->read_buffer.empty())
        {
            return;
        }
        if (conn->read_buffer.size() > kMaxRequestBytes)
        {
            // The request cannot be safely resynchronized once the buffer limit
            // is exceeded. Discard it and force the error response to close the
            // connection instead of reprocessing the same bytes.
            conn->read_buffer.clear();
            conn->request.Reset();
            conn->response.Reset();
            conn->response.SetStatusCode(Http::StatusCode::PayloadTooLarge);
            conn->response.SetContentType("text/plain");
            conn->response.SetBody("Request exceeds the maximum size");
            SendResponse(conn);
            return;
        }

        // 解析HTTP请求 - 使用累积的缓冲区数据（std::string 隐式转为 std::span<const char>）
        if (!conn->parser.Parse(conn->read_buffer, conn->request))
        {
            SendErrorResponse(conn);
            return;
        }

        // 检查是否解析完成
        if (conn->parser.HasError())
        {
            SendErrorResponse(conn);
            return;
        }

        if (!conn->parser.NeedMoreData())
        {
            // 清除已解析的数据
            size_t parsed_bytes = conn->parser.GetParsedBytes();
            if (parsed_bytes > 0 && parsed_bytes <= conn->read_buffer.size())
            {
                conn->read_buffer.erase(0, parsed_bytes);
            }

            // 重置解析器状态，为下一个请求做准备
            conn->parser.Reset();

            OnHttpRequest(conn);
        }
    }

    // http请求处理
    void SubReactor::OnHttpRequest(TcpConnectionPtr conn)
    {
        // 快速解析头部，分类任务
        if (IsIoTask(conn->request))
        {
            // IO任务，直接在事件循环中处理
            ProcessIoTask(conn);
        }
        else
        {
            // CPU任务，加入CPU队列
            cpu_thread_pool_->AddTask([this, conn]()
                                      {
                                ProcessCpuTask(conn);
                                // 任务完成后，回调到事件循环发送响应
                                sub_loop_.QueueInLoop([this, conn]()
                                                    { SendResponse(conn); 
                                                    }); });
        }
    }

    // 获取文件内容类型
    static std::string GetContentType(const std::string &file_path)
    {
        std::string ext = std::filesystem::path(file_path).extension().string();
        if (ext == ".html" || ext == ".htm")
            return "text/html";
        if (ext == ".css")
            return "text/css";
        if (ext == ".js")
            return "application/javascript";
        if (ext == ".png")
            return "image/png";
        if (ext == ".jpg" || ext == ".jpeg")
            return "image/jpeg";
        if (ext == ".gif")
            return "image/gif";
        return "application/octet-stream";
    }

    // 判断请求是否要求长连接。Connection 头的值大小写不敏感（keep-alive / Keep-Alive / KEEP-ALIVE 均合法），
    // 且沿用旧的「子串匹配」语义（值里含 keep-alive 即复用）。改用 GetHeader 返回的 string_view 直接
    // 做大小写不敏感子串查找，省掉旧实现里的 std::string 拷贝 + 全量小写化。
    static bool WantsKeepAlive(const Http::HttpRequest &req)
    {
        const std::string_view value = req.GetHeader("Connection");
        constexpr std::string_view kToken = "keep-alive";

        return std::search(value.begin(), value.end(), kToken.begin(), kToken.end(),
                           [](unsigned char a, unsigned char b)
                           { return std::tolower(a) == std::tolower(b); }) != value.end();
    }

    static const char *const kNotFoundBody =
        "<!DOCTYPE html><html><body><h1>404 Not Found</h1><p>The requested file was not found on this server.</p></body></html>";

    // 把 URL 路径解析为静态根目录下的安全文件路径。
    // 关键点：先归一化（消解 ".."、"." 与符号链接），再校验结果必须仍落在根目录内，
    // 否则形如 /../../etc/passwd 的请求会读到根目录之外的文件。
    // 越界时返回 false，由调用方按 404 处理（不泄漏"路径非法"这一信息）。
    static bool ResolveSafePath(const std::string &root_dir, std::string_view url_path, std::string &out)
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        fs::path root = fs::weakly_canonical(fs::path(root_dir), ec);
        if (ec)
        {
            return false;
        }

        // URL 路径由解析器保证以 '/' 开头，去掉后拼到根目录下
        fs::path full = fs::weakly_canonical(root / fs::path(url_path.substr(1)), ec);
        if (ec)
        {
            return false;
        }

        const std::string root_str = root.string();
        const std::string full_str = full.string();

        // 必须是根目录本身，或根目录 + 路径分隔符开头的子路径。
        // 只比前缀不够：/var/wwwroot 与 /var/www 也会前缀相同，故需校验分隔符。
        if (full_str != root_str &&
            (full_str.size() <= root_str.size() ||
             !full_str.starts_with(root_str) ||
             full_str[root_str.size()] != static_cast<char>(fs::path::preferred_separator)))
        {
            return false;
        }

        out = full_str;
        return true;
    }

    // 处理I/O密集型任务（静态文件服务）
    void SubReactor::ProcessIoTask(TcpConnectionPtr conn)
    {
        std::string_view path = conn->request.GetPath();

        if (path == "/")
        {
            path = "/index.html";
        }

        // 构建完整文件路径（越界路径直接判定为不可用）
        std::string file_path;
        const bool in_root = ResolveSafePath(root_dir_, path, file_path);

        try
        {
            // 检查路径是否越界、文件是否存在
            if (!in_root || !std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
            {
                // 文件不存在，返回404
                conn->response.SetStatusCode(Http::StatusCode::NotFound);
                conn->response.SetContentType("text/html");
                conn->response.SetBody(kNotFoundBody);
            }
            else
            {
                // 打开文件并读取内容
                std::ifstream file(file_path, std::ios::binary);
                if (!file.is_open())
                {
                    // 文件打开失败，返回500
                    conn->response.SetStatusCode(Http::StatusCode::InternalServerError);
                    conn->response.SetContentType("text/html");
                    conn->response.SetBody("<!DOCTYPE html><html><body><h1>500 Internal Server Error</h1><p>Failed to open file.</p></body></html>");
                }
                else
                {
                    size_t file_size = std::filesystem::file_size(file_path);
                    std::string body(file_size, '\0');
                    file.read(body.data(), file_size);

                    // 文件可能在 stat 之后被并发截断：read 实际读到的字节数可能少于
                    // file_size，此时 body 尾部会残留预置的 NUL。用 gcount() 取真实读入量，
                    // 按它收尾，避免响应体夹 NUL、Content-Length 虚高。
                    const std::streamsize got = file.gcount();
                    if (got < static_cast<std::streamsize>(file_size))
                    {
                        file_size = got > 0 ? static_cast<size_t>(got) : 0;
                        body.resize(file_size);
                    }

                    // 设置响应
                    conn->response.SetStatusCode(Http::StatusCode::OK);
                    conn->response.SetContentType(GetContentType(file_path));
                    conn->response.SetContentLength(file_size);
                    conn->response.SetBody(body);

                    file.close();
                }
            }
        }
        catch (const std::exception &e)
        {
            // 其他异常，返回500
            conn->response.SetStatusCode(Http::StatusCode::InternalServerError);
            conn->response.SetContentType("text/html");
            conn->response.SetBody("<!DOCTYPE html><html><body><h1>500 Internal Server Error</h1><p>" + std::string(e.what()) + "</p></body></html>");
        }

        sub_loop_.QueueInLoop([this, conn]()
                              { SendResponse(conn); });
    }

    // 处理CPU任务
    void SubReactor::ProcessCpuTask(TcpConnectionPtr conn)
    {
        std::string_view path = conn->request.GetPath();
        Http::Method method = conn->request.GetMethod();

        // 处理OPTIONS预检请求
        if (method == Http::Method::OPTIONS && (path == "/login" || path == "/register"))
        {
            SendJsonResponse(conn, Http::StatusCode::OK, "{}");
            return;
        }

        // 检查是否是POST请求到/login或/register
        if (method == Http::Method::POST && (path == "/login" || path == "/register"))
        {
            HandleAuthRequest(conn, path);
        }
        else
        {
            // 其他CPU任务
            conn->response.SetStatusCode(Http::StatusCode::OK);
            conn->response.AddHeader("Content-Type", "text/plain");
            conn->response.SetBody("Hello, World! (CPU Task)");
        }
    }

    // 处理认证请求（登录/注册）
    void SubReactor::HandleAuthRequest(TcpConnectionPtr conn, std::string_view path)
    {
        try
        {
            // 解析POST请求体（JSON格式）
            std::string body = conn->request.GetBody();

            // 简单的JSON解析（用户名和密码）。
            // 只定位字段名，冒号与引号的位置在此后单独查找，因此 "username" : "x"
            // 这类带空格的写法同样能解析。不支持转义引号等完整 JSON 语法。
            std::string username, password;
            size_t user_pos = body.find("\"username\"");
            size_t pass_pos = body.find("\"password\"");

            if (user_pos != std::string::npos && pass_pos != std::string::npos)
            {
                // 解析用户名
                size_t user_colon_pos = body.find(':', user_pos);
                if (user_colon_pos != std::string::npos)
                {
                    size_t user_quote_start = body.find('"', user_colon_pos);
                    if (user_quote_start != std::string::npos)
                    {
                        size_t user_quote_end = body.find('"', user_quote_start + 1);
                        if (user_quote_end != std::string::npos)
                        {
                            username = body.substr(user_quote_start + 1, user_quote_end - user_quote_start - 1);
                        }
                    }
                }

                // 解析密码
                size_t pass_colon_pos = body.find(':', pass_pos);
                if (pass_colon_pos != std::string::npos)
                {
                    size_t pass_quote_start = body.find('"', pass_colon_pos);
                    if (pass_quote_start != std::string::npos)
                    {
                        size_t pass_quote_end = body.find('"', pass_quote_start + 1);
                        if (pass_quote_end != std::string::npos)
                        {
                            password = body.substr(pass_quote_start + 1, pass_quote_end - pass_quote_start - 1);
                        }
                    }
                }
            }

            if (username.empty() || password.empty())
            {
                SendJsonResponse(conn, Http::StatusCode::BadRequest, "{\"success\": false, \"message\": \"Invalid parameters\"}");
                return;
            }

            // 获取数据库连接池实例
            auto &db_pool = DB::MysqlConnectionPool::GetInstance();

            if (path == "/login")
            {
                // 处理登录
                bool login_success = db_pool.VerifyLogin(username, password);
                if (login_success)
                {
                    SendJsonResponse(conn, Http::StatusCode::OK, "{\"success\": true, \"message\": \"Login successful\"}");
                }
                else
                {
                    SendJsonResponse(conn, Http::StatusCode::Unauthorized, "{\"success\": false, \"message\": \"Invalid username or password\"}");
                }
            }
            else if (path == "/register")
            {
                // 处理注册
                bool register_success = db_pool.RegisterUser(username, password);
                if (register_success)
                {
                    SendJsonResponse(conn, Http::StatusCode::OK, "{\"success\": true, \"message\": \"Registration successful\"}");
                }
                else
                {
                    SendJsonResponse(conn, Http::StatusCode::BadRequest, "{\"success\": false, \"message\": \"Username already exists\"}");
                }
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "HandleAuthRequest failed for " << path << ": " << e.what();
            SendJsonResponse(conn, Http::StatusCode::InternalServerError, "{\"success\": false, \"message\": \"Internal server error\"}");
        }
    }

    // 设置JSON响应（仅设置响应头和内容，不发送——由事件循环线程统一发送）
    void SubReactor::SendJsonResponse(TcpConnectionPtr conn, Http::StatusCode status_code, const std::string &json_body)
    {
        conn->response.SetStatusCode(status_code);
        conn->response.AddHeader("Content-Type", "application/json");
        conn->response.AddHeader("Access-Control-Allow-Origin", "*");
        conn->response.AddHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        conn->response.AddHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        conn->response.AddHeader("Access-Control-Max-Age", "86400");
        conn->response.SetBody(json_body);
        // 注意：不在这里调用 SendResponse(conn)
        // SendResponse 统一由 OnHttpRequest 中的 QueueInLoop 在事件循环线程中调用
        // 避免 CPU 线程池线程和事件循环线程并发写 socket / 修改 epoll
    }

    // 判断请求类型。
    // 这里是**路由判断的唯一真相**——HttpRequest 里那份重复的 task_type_ 已删除，
    // 不要再引第二份：两者语义并不相同（那份把所有非 GET/HEAD 都判为 CPU，这份只把
    // /login、/register 判为 CPU），"合并重复逻辑"会变成一次静默的路由行为变更。
    bool SubReactor::IsIoTask(const Http::HttpRequest &req)
    {
        std::string_view path = req.GetPath();
        Http::Method method = req.GetMethod();

        // OPTIONS请求到/login或/register需要CPU处理（用于CORS预检）
        if (method == Http::Method::OPTIONS && (path == "/login" || path == "/register"))
        {
            return false;
        }

        // POST请求到/login或/register需要CPU处理
        if (method == Http::Method::POST && (path == "/login" || path == "/register"))
        {
            return false;
        }

        // 其他请求作为I/O任务处理
        return true;
    }

    // 发送响应
    void SubReactor::SendResponse(TcpConnectionPtr conn)
    {
        if (conn->closed)
        {
            return;
        }

        // 检查是否需要保持长连接
        if (WantsKeepAlive(conn->request))
        {
            conn->response.AddHeader("Connection", "keep-alive");
            conn->response.AddHeader("Keep-Alive", "timeout=30, max=1000");
        }
        else
        {
            conn->response.AddHeader("Connection", "close");
        }

        // 生成响应字符串，保存到写缓冲区
        conn->write_buffer = conn->response.ToString();
        if (conn->request.GetMethod() == Http::Method::HEAD)
        {
            const size_t body_start = conn->write_buffer.find("\r\n\r\n");
            if (body_start != std::string::npos)
            {
                conn->write_buffer.resize(body_start + 4);
            }
        }
        conn->write_offset = 0;

        // 解除读事件，注册写事件
        sub_loop_.UpdateChannel(conn->fd, EventLoop::EventType::kWriteEvent,
                                nullptr,
                                std::bind(&SubReactor::OnWrite, this, conn));

        // 立即尝试一次写入（大部分情况一次写完，避免额外的 epoll 循环）
        OnWrite(conn);
    }

    // 写事件回调：在 socket 可写时继续发送剩余数据
    void SubReactor::OnWrite(TcpConnectionPtr conn)
    {
        if (conn->closed)
        {
            return;
        }

        const char *data = conn->write_buffer.c_str();
        size_t total_size = conn->write_buffer.size();
        size_t &offset = conn->write_offset;

        // 循环写入直到完成或遇到 EAGAIN
        while (offset < total_size)
        {
            ssize_t nwritten = write(conn->fd, data + offset, total_size - offset);

            if (nwritten > 0)
            {
                offset += nwritten;
            }
            else if (nwritten == 0)
            {
                // 对端关闭
                CloseConnection(conn);
                return;
            }
            else // nwritten < 0
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // socket 缓冲区满，等待下次 EPOLLOUT 事件再写
                    // 写事件已经注册，epoll 会在可写时再次回调 OnWrite
                    return;
                }
                else
                {
                    // 其他写入错误
                    CloseConnection(conn);
                    return;
                }
            }
        }

        // 全部写入完成
        conn->write_buffer.clear();
        conn->write_offset = 0;

        // 检查是否需要保持长连接
        if (WantsKeepAlive(conn->request))
        {
            conn->parser.Reset();
            conn->response.Reset();
            // 请求对象也必须一起清空——conn->request 是整条连接共用的同一个对象。
            // 漏掉这一句，上一请求的 Content-Length 就会残留：带请求体的
            // POST /register(Content-Length: 40) 之后，同一条 keep-alive 连接上的
            // GET /login.html 会被解析器当成「还要再等 40 字节 body」，永远停在
            // BODY 状态，NeedMoreData() 恒为真 → OnHttpRequest() 不再被调用 →
            // 服务端对此后所有请求都不回应，浏览器跳转永久挂起。
            // 顺序有讲究：上面刚用 conn->request 判过 WantsKeepAlive，所以只能放在
            // 两个 Reset 之后。
            conn->request.Reset();

            // 切换回读事件
            sub_loop_.UpdateChannel(conn->fd, EventLoop::EventType::kReadEvent,
                                    std::bind(&SubReactor::OnRead, this, conn));

            // A pipelined request may already be buffered in user space after the
            // previous read. The kernel will not emit another EPOLLIN event for it,
            // so schedule parsing explicitly after switching back to read mode.
            if (!conn->read_buffer.empty())
            {
                sub_loop_.QueueInLoop([this, conn]() { OnRead(conn); });
            }
        }
        else
        {
            CloseConnection(conn);
        }
    }

    // 发送错误响应
    void SubReactor::SendErrorResponse(TcpConnectionPtr conn)
    {
        conn->response.SetStatusCode(Http::StatusCode::BadRequest);
        conn->response.AddHeader("Content-Type", "text/plain");
        conn->response.SetBody("Bad Request");

        SendResponse(conn);
    }

    // 关闭连接
    void SubReactor::CloseConnection(TcpConnectionPtr conn)
    {
        if (conn->closed)
        {
            return;
        }
        conn->closed = true;

        // 保存fd，在关闭前先从连接表中移除
        int fd = conn->fd;

        // 1. 先从连接表中移除（用保存的fd）
        connections_.erase(fd);
        connection_count_.fetch_sub(1, std::memory_order_release);

        // 2. 从epoll中移除事件监听
        sub_loop_.RemoveChannel(fd);

        // 3. 关闭文件描述符
        if (fd >= 0)
        {
            close(fd);
            conn->fd = -1; // 标记为已关闭
        }
    }

    // 空闲连接检查（由定时器周期性调用）
    void SubReactor::CheckIdleConnections()
    {
        auto now = std::chrono::steady_clock::now();
        auto idle_timeout = std::chrono::seconds(idle_timeout_seconds_);

        std::vector<TcpConnectionPtr> idle_connections;

        // 收集空闲连接（在遍历时不能直接关闭，避免迭代器失效）
        for (auto &[fd, conn] : connections_)
        {
            if (conn && !conn->closed)
            {
                auto idle_duration = std::chrono::duration_cast<std::chrono::seconds>(
                    now - conn->last_activity);
                if (idle_duration >= idle_timeout)
                {
                    idle_connections.push_back(conn);
                }
            }
        }

        // 关闭所有空闲连接
        if (!idle_connections.empty())
        {
            LOG_INFO << "Closing " << idle_connections.size()
                     << " idle connections (timeout=" << idle_timeout_seconds_ << "s)";
            for (auto &conn : idle_connections)
            {
                CloseConnection(conn);
            }
        }
    }
}

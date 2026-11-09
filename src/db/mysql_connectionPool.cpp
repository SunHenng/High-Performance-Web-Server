#include "db/mysql_connectionPool.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <openssl/evp.h>
#include <iomanip>

namespace DB
{

    // 单例模式获取实例
    MysqlConnectionPool &MysqlConnectionPool::GetInstance()
    {
        static MysqlConnectionPool instance;
        return instance;
    }

    // 私有构造函数
    MysqlConnectionPool::MysqlConnectionPool()
        : conn_count_(0),
          free_count_(0),
          is_initialized_(false),
          is_closed_(false)
    {
    }

    // 初始化连接池
    void MysqlConnectionPool::Init(const std::string &host, const std::string &user,
                                   const std::string &password, const std::string &database,
                                   unsigned int port, unsigned int max_conn, unsigned int min_conn)
    {
        if (is_initialized_)
        {
            throw std::runtime_error("Connection pool is already initialized");
        }

        host_ = host;
        user_ = user;
        password_ = password;
        database_ = database;
        port_ = port;
        max_conn_ = max_conn;
        min_conn_ = min_conn;

        std::cerr << "Initializing database connection pool with:" << std::endl;
        std::cerr << "  Host: " << host_ << std::endl;
        std::cerr << "  User: " << user_ << std::endl;
        std::cerr << "  Database: " << database_ << std::endl;
        std::cerr << "  Port: " << port_ << std::endl;
        std::cerr << "  Min connections: " << min_conn_ << std::endl;
        std::cerr << "  Max connections: " << max_conn_ << std::endl;

        if (min_conn_ > max_conn_)
        {
            min_conn_ = max_conn_;
        }

        // 创建初始连接
        for (unsigned int i = 0; i < min_conn_; ++i)
        {
            MYSQL *conn = CreateConnection();
            if (conn)
            {
                connection_queue_.push(conn);
                ++free_count_;
                std::cerr << "Created connection " << (i + 1) << "/" << min_conn_ << std::endl;
            }
            else
            {
                std::cerr << "Failed to create connection " << (i + 1) << "/" << min_conn_ << std::endl;
            }
        }

        is_initialized_ = true;
        std::cerr << "Database connection pool initialized successfully. Total connections: " << conn_count_ << std::endl;
    }

    // 创建连接
    MYSQL *MysqlConnectionPool::CreateConnection()
    {
        MYSQL *conn = mysql_init(nullptr);
        if (!conn)
        {
            // conn 为 null 时不能交给 mysql_error()，那会解引用空指针
            std::cerr << "mysql_init failed: out of memory" << std::endl;
            return nullptr;
        }

        // 设置连接超时
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, "3");

        // 设置字符集
        mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        // 连接数据库
        std::cerr << "Connecting to database: " << host_ << ":" << port_ << "/" << database_ << std::endl;

        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), password_.c_str(),
                                database_.c_str(), port_, nullptr, 0))
        {
            std::cerr << "mysql_real_connect failed: " << mysql_error(conn) << std::endl;
            mysql_close(conn);
            return nullptr;
        }

        std::cerr << "Database connection established successfully" << std::endl;
        ++conn_count_;
        return conn;
    }

    // 获取连接
    std::shared_ptr<MYSQL> MysqlConnectionPool::GetConnection(int timeout_ms)
    {
        if (!is_initialized_ || is_closed_)
        {
            throw std::runtime_error("Connection pool is not initialized or has been closed");
        }

        std::unique_lock<std::mutex> lock(mutex_);

        // 等待可用连接
        bool has_conn = cond_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                       [this]()
                                       { return !connection_queue_.empty() || conn_count_ < max_conn_; });

        if (!has_conn)
        {
            throw std::runtime_error("Get connection timeout");
        }

        MYSQL *conn = nullptr;

        // 如果有空闲连接，直接使用
        if (!connection_queue_.empty())
        {
            conn = connection_queue_.front();
            connection_queue_.pop();
            --free_count_;

            // 检查连接是否有效
            if (mysql_ping(conn) != 0)
            {
                std::cerr << "Connection is invalid, recreate it" << std::endl;
                mysql_close(conn);
                conn = CreateConnection();
            }
        }
        else if (conn_count_ < max_conn_)
        {
            // 创建新连接
            conn = CreateConnection();
        }

        if (!conn)
        {
            throw std::runtime_error("Create connection failed");
        }

        // 使用自定义删除器，将连接放回连接池而不是直接关闭
        return std::shared_ptr<MYSQL>(conn, [this](MYSQL *conn)
                                      {
        if (conn) {
            ReleaseConnection(conn);
        } });
    }

    // 释放连接
    void MysqlConnectionPool::ReleaseConnection(MYSQL *conn)
    {
        if (!conn || is_closed_)
        {
            return;
        }

        std::unique_lock<std::mutex> lock(mutex_);

        // 检查连接是否有效
        if (mysql_ping(conn) == 0)
        {
            // 将连接放回队列
            connection_queue_.push(conn);
            ++free_count_;

            // 唤醒等待的线程
            cond_.notify_one();
        }
        else
        {
            // 连接无效，直接关闭
            mysql_close(conn);
            --conn_count_;
        }
    }

    // 关闭连接池
    void MysqlConnectionPool::ClosePool()
    {
        if (!is_initialized_ || is_closed_)
        {
            return;
        }

        is_closed_ = true;

        // 唤醒所有等待的线程
        cond_.notify_all();

        // 销毁所有连接
        DestroyAllConnections();

        is_initialized_ = false;
    }

    // 销毁所有连接
    void MysqlConnectionPool::DestroyAllConnections()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        while (!connection_queue_.empty())
        {
            MYSQL *conn = connection_queue_.front();
            connection_queue_.pop();
            mysql_close(conn);
            --conn_count_;
            --free_count_;
        }
    }

    // 获取当前连接数
    size_t MysqlConnectionPool::GetConnectionCount()
    {
        return conn_count_;
    }

    // 获取空闲连接数
    size_t MysqlConnectionPool::GetFreeConnectionCount()
    {
        return free_count_;
    }

    // SHA256加密（使用 EVP 接口，兼容 OpenSSL 3.0+）
    std::string SHA256(const std::string &input)
    {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx)
        {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;

        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, input.c_str(), input.length());
        EVP_DigestFinal_ex(ctx, hash, &hash_len);
        EVP_MD_CTX_free(ctx);

        std::stringstream ss;
        for (unsigned int i = 0; i < hash_len; ++i)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    // 登录校验
    bool MysqlConnectionPool::VerifyLogin(const std::string &username, const std::string &password)
    {
        try
        {
            auto conn = GetConnection();

            // 准备SQL语句（使用预处理防止SQL注入）
            MYSQL_STMT *stmt = mysql_stmt_init(conn.get());
            if (!stmt)
            {
                std::cerr << "mysql_stmt_init failed" << std::endl;
                return false;
            }

            std::string sql = "SELECT password FROM users WHERE username = ?";
            if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0)
            {
                std::cerr << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 绑定参数
            MYSQL_BIND bind_param;
            memset(&bind_param, 0, sizeof(MYSQL_BIND));
            bind_param.buffer_type = MYSQL_TYPE_STRING;
            bind_param.buffer = (char *)username.c_str();
            bind_param.buffer_length = username.length();

            if (mysql_stmt_bind_param(stmt, &bind_param) != 0)
            {
                std::cerr << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 执行查询
            if (mysql_stmt_execute(stmt) != 0)
            {
                std::cerr << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 绑定结果
            MYSQL_BIND bind_result;
            memset(&bind_result, 0, sizeof(MYSQL_BIND));

            char password_hash[65]; // SHA256哈希值是64个字符，加上结束符
            memset(password_hash, 0, sizeof(password_hash));

            bind_result.buffer_type = MYSQL_TYPE_STRING;
            bind_result.buffer = password_hash;
            bind_result.buffer_length = sizeof(password_hash);

            unsigned long password_hash_len = 0;
            bind_result.length = &password_hash_len;

            if (mysql_stmt_bind_result(stmt, &bind_result) != 0)
            {
                std::cerr << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 获取结果
            bool found = false;
            std::string db_password_hash;

            while (mysql_stmt_fetch(stmt) == 0)
            {
                found = true;
                db_password_hash = std::string(password_hash, password_hash_len);
                break;
            }

            mysql_stmt_close(stmt);

            if (!found)
            {
                return false; // 用户不存在
            }

            // 验证密码
            std::string input_password_hash = SHA256(password);
            return input_password_hash == db_password_hash;
        }
        catch (const std::exception &e)
        {
            std::cerr << "VerifyLogin failed: " << e.what() << std::endl;
            return false;
        }
    }

    // 用户注册
    bool MysqlConnectionPool::RegisterUser(const std::string &username, const std::string &password)
    {
        try
        {
            auto conn = GetConnection();

            // 准备SQL语句（使用预处理防止SQL注入）
            MYSQL_STMT *stmt = mysql_stmt_init(conn.get());
            if (!stmt)
            {
                std::cerr << "RegisterUser: mysql_stmt_init failed" << std::endl;
                return false;
            }

            // 检查用户名是否已存在
            std::string check_sql = "SELECT COUNT(*) FROM users WHERE username = ?";
            if (mysql_stmt_prepare(stmt, check_sql.c_str(), check_sql.length()) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_prepare failed for check: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            MYSQL_BIND check_param;
            memset(&check_param, 0, sizeof(MYSQL_BIND));
            check_param.buffer_type = MYSQL_TYPE_STRING;
            check_param.buffer = (char *)username.c_str();
            check_param.buffer_length = username.length();

            if (mysql_stmt_bind_param(stmt, &check_param) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_bind_param failed for check: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            if (mysql_stmt_execute(stmt) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_execute failed for check: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            MYSQL_BIND check_result;
            memset(&check_result, 0, sizeof(MYSQL_BIND));
            unsigned long count = 0;
            check_result.buffer_type = MYSQL_TYPE_LONG;
            check_result.buffer = &count;

            if (mysql_stmt_bind_result(stmt, &check_result) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            if (mysql_stmt_fetch(stmt) == 0 && count > 0)
            {
                // 用户名已存在：正常业务分支，不记录日志
                mysql_stmt_close(stmt);
                return false; // 用户名已存在
            }

            mysql_stmt_close(stmt);

            // 插入新用户
            stmt = mysql_stmt_init(conn.get());
            if (!stmt)
            {
                std::cerr << "RegisterUser: mysql_stmt_init failed for INSERT" << std::endl;
                return false;
            }

            std::string insert_sql = "INSERT INTO users (username, password) VALUES (?, ?)";
            if (mysql_stmt_prepare(stmt, insert_sql.c_str(), insert_sql.length()) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_prepare failed for INSERT: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 只存 SHA256 摘要，不落明文密码
            std::string password_hash = SHA256(password);

            // 绑定参数
            MYSQL_BIND bind_params[2];
            memset(bind_params, 0, sizeof(bind_params));

            // 用户名参数
            bind_params[0].buffer_type = MYSQL_TYPE_STRING;
            bind_params[0].buffer = (char *)username.c_str();
            bind_params[0].buffer_length = username.length();

            // 密码哈希参数
            bind_params[1].buffer_type = MYSQL_TYPE_STRING;
            bind_params[1].buffer = (char *)password_hash.c_str();
            bind_params[1].buffer_length = password_hash.length();

            if (mysql_stmt_bind_param(stmt, bind_params) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_bind_param failed for INSERT: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 执行插入
            if (mysql_stmt_execute(stmt) != 0)
            {
                std::cerr << "RegisterUser: mysql_stmt_execute failed for INSERT: " << mysql_stmt_error(stmt) << std::endl;
                mysql_stmt_close(stmt);
                return false;
            }

            // 检查影响的行数
            my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
            mysql_stmt_close(stmt);

            return affected_rows > 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << "RegisterUser failed with exception: " << e.what() << std::endl;
            return false;
        }
    }

    // 析构函数
    MysqlConnectionPool::~MysqlConnectionPool()
    {
        ClosePool();
    }

} // namespace DB

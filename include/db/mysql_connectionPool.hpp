#pragma once

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <functional>
#include <cstring>

namespace DB
{

    class MysqlConnectionPool
    {
    public:
        // 单例模式获取实例
        static MysqlConnectionPool &GetInstance();

        // 禁止拷贝和移动
        MysqlConnectionPool(const MysqlConnectionPool &) = delete;
        MysqlConnectionPool &operator=(const MysqlConnectionPool &) = delete;
        MysqlConnectionPool(MysqlConnectionPool &&) = delete;
        MysqlConnectionPool &operator=(MysqlConnectionPool &&) = delete;

        // 初始化连接池
        void Init(const std::string &host, const std::string &user,
                  const std::string &password, const std::string &database,
                  unsigned int port, unsigned int max_conn, unsigned int min_conn = 5);

        // 获取连接
        std::shared_ptr<MYSQL> GetConnection(int timeout_ms = 3000);

        // 关闭连接池
        void ClosePool();

        // 获取当前连接数
        size_t GetConnectionCount();

        // 获取空闲连接数
        size_t GetFreeConnectionCount();

        // 登录校验
        bool VerifyLogin(const std::string &username, const std::string &password);

        // 用户注册
        bool RegisterUser(const std::string &username, const std::string &password);

        // 析构函数
        ~MysqlConnectionPool();

    private:
        // 私有构造函数
        MysqlConnectionPool();

        // 创建连接
        MYSQL *CreateConnection();

        // 释放连接
        void ReleaseConnection(MYSQL *conn);

        // 销毁所有连接
        void DestroyAllConnections();

        // 连接池参数
        std::string host_;
        std::string user_;
        std::string password_;
        std::string database_;
        unsigned int port_;
        unsigned int max_conn_;
        unsigned int min_conn_;

        // 连接队列
        std::queue<MYSQL *> connection_queue_;

        // 线程安全相关
        std::mutex mutex_;
        std::condition_variable cond_;

        // 连接计数
        std::atomic<unsigned int> conn_count_;
        std::atomic<unsigned int> free_count_;

        // 连接池状态
        std::atomic<bool> is_initialized_;
        std::atomic<bool> is_closed_;
    };

} // namespace DB

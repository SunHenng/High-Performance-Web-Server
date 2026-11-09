#pragma once

#include "config.hpp"
#include "Reactors/ReactorManager.hpp"
#include "log/Logger.hpp"
#include "log/AsynLogging.hpp"
#include "db/mysql_connectionPool.hpp"

#include <signal.h>
#include <memory>
#include <iostream>

class ModernWebServer
{
private:
    std::unique_ptr<Reactors::ReactorManager> reactor_manager_;
    std::unique_ptr<logging::AsynLogging> asyn_logging_;
    config server_config_;
    bool running_ = false;

public:
    ModernWebServer(const char *argv0) : server_config_(argv0) {}

    ~ModernWebServer()
    {
        Stop();
    }

    bool Initialize(int argc = 0, char *argv[] = nullptr)
    {
        try
        {
            // 解析命令行参数
            server_config_.parse_arg(argc, argv);

            // 初始化异步日志
            asyn_logging_ = std::make_unique<logging::AsynLogging>(
                server_config_.log_name,
                server_config_.log_file_size,
                server_config_.log_file_count);

            logging::Logger::SetOuput([this](const std::string &msg)
                                      { asyn_logging_->append(msg); });

            logging::Logger::SetFlush([this]()
                                      { asyn_logging_->flush(); });

            logging::Logger::SetLogLevel(logging::LOG_LEVEL::INFO);
            asyn_logging_->start();

            // 初始化数据库连接池
            auto &db_pool = DB::MysqlConnectionPool::GetInstance();
            try
            {
                db_pool.Init(
                    server_config_.db_host,
                    server_config_.db_user,
                    server_config_.db_password,
                    server_config_.db_database,
                    server_config_.db_port,
                    server_config_.db_max_conn,
                    server_config_.db_min_conn);
                LOG_INFO << "Database connection pool initialized successfully";
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "Failed to initialize database connection pool: " << e.what();
                return false;
            }

            // 创建Reactor管理器
            reactor_manager_ = std::make_unique<Reactors::ReactorManager>(
                server_config_.sub_reactor_count,
                server_config_.root_dir,
                server_config_.idle_timeout,
                server_config_.max_connections);

            // 初始化Reactor管理器
            reactor_manager_->Initialize();

            // 设置监听端口
            if (!reactor_manager_->GetMainReactor().SetListenSocket(server_config_.port))
            {
                LOG_ERROR << "Failed to set up listening socket on port " << server_config_.port;
                return false;
            }

            // 设置信号处理
            signal(SIGINT, SignalHandler);
            signal(SIGTERM, SignalHandler);

            running_ = true;

            LOG_INFO << "Server initialized successfully";
            server_config_.print_config();

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to initialize server: " << e.what() << std::endl;
            return false;
        }
    }

    void Start()
    {
        if (!running_)
        {
            std::cerr << "Server not initialized. Call Initialize() first." << std::endl;
            return;
        }

        LOG_INFO << "Starting server on port " << server_config_.port;

        // 启动所有Reactor
        reactor_manager_->Start();

        LOG_INFO << "Server started successfully";
    }

    void Stop()
    {
        if (running_)
        {
            LOG_INFO << "Stopping server...";

            if (reactor_manager_)
            {
                reactor_manager_->Stop();
            }

            running_ = false;
            LOG_INFO << "Server stopped";
        }
    }

    const config &GetConfig() const { return server_config_; }

private:
    static void SignalHandler(int sig)
    {
        if (sig == SIGINT || sig == SIGTERM)
        {
            std::cout << "\nReceived signal " << sig << ", shutting down server..." << std::endl;
            exit(0);
        }
    }
};
#pragma once

#include <thread>
#include <string>
#include <iostream>
#include <getopt.h>
#include <filesystem>
#include <unistd.h>
#include <limits.h>
#include <cstdlib>

inline std::string DefaultDbPassword()
{
    const char *env = std::getenv("MWS_DB_PASSWORD");
    return (env && *env) ? std::string(env) : std::string("change_me");
}

class config
{
public:
    int port = 7070;
    int sub_reactor_count = std::thread::hardware_concurrency();
    std::string root_dir; // = "static/";

    // 数据库
    std::string db_host = "localhost";
    std::string db_user = "root";
    std::string db_password = DefaultDbPassword();
    std::string db_database = "testdb";
    unsigned int db_port = 3306;
    unsigned int db_max_conn = 50;
    unsigned int db_min_conn = 5;

    // 连接超时配置
    int idle_timeout = 30;   // 空闲连接超时秒数，默认30秒
    int max_connections = 10000; // 最大并发连接数

    // 日志配置
    std::string log_name = "ModernWebServer";
    size_t log_file_size = 1024 * 1024; // 1MB
    int log_file_count = 3;

    std::string GetRootDirFromArgv0(const char *argv0)
    {
        auto exe = std::filesystem::canonical(argv0);
        auto root = exe.parent_path().parent_path() / "static/";
        return root.string();
    }

    config(const char *argv0)
    {
        root_dir = GetRootDirFromArgv0(argv0);
    }
public:
    void parse_arg(int argc, char *argv[])
    {
        int opt;
        const char *str = "p:r:t:h:u:P:d:m:n:l:s:c:i:M:";

        while ((opt = getopt(argc, argv, str)) != -1)
        {
            switch (opt)
            {
            case 'p': // 端口
                port = std::stoi(optarg);
                break;
            case 'r': // 根目录
                root_dir = optarg;
                break;
            case 't': // 子反应器数量
                sub_reactor_count = std::stoi(optarg);
                break;
            case 'h': // 数据库主机
                db_host = optarg;
                break;
            case 'u': // 数据库用户
                db_user = optarg;
                break;
            case 'P': // 数据库密码
                db_password = optarg;
                break;
            case 'd': // 数据库名
                db_database = optarg;
                break;
            case 'm': // 数据库端口
                db_port = std::stoi(optarg);
                break;
            case 'n': // 最大连接数
                db_max_conn = std::stoi(optarg);
                break;
            case 'l': // 最小连接数
                db_min_conn = std::stoi(optarg);
                break;
            case 's': // 日志文件大小
                log_file_size = std::stoul(optarg);
                break;
            case 'c': // 日志文件数量
                log_file_count = std::stoi(optarg);
                break;
            case 'i': // 空闲连接超时（秒）
                idle_timeout = std::stoi(optarg);
                break;
            case 'M': // 最大并发连接数
                max_connections = std::stoi(optarg);
                break;
            default:
                print_usage();
                exit(EXIT_FAILURE);
            }
        }
    }

    void print_usage() const
    {
        std::cout << "Usage: ModernWebServer [options]\n"
                  << "Options:\n"
                  << "  -p <port>           Server port (default: 7070)\n"
                  << "  -r <root_dir>       Root directory (default: static/)\n"
                  << "  -t <threads>        Number of sub-reactors (default: hardware_concurrency)\n"
                  << "  -h <host>           Database host (default: localhost)\n"
                  << "  -u <user>           Database user (default: root)\n"
                  << "  -P <password>       Database password (default: MWS_DB_PASSWORD or change_me)\n"
                  << "  -d <database>       Database name (default: testdb)\n"
                  << "  -m <port>           Database port (default: 3306)\n"
                  << "  -n <max_conn>       Database max connections (default: 50)\n"
                  << "  -l <min_conn>       Database min connections (default: 5)\n"
                  << "  -s <file_size>      Log file size in bytes (default: 1048576)\n"
                  << "  -c <file_count>     Log file count (default: 3)\n"
                  << "  -i <seconds>        Idle connection timeout in seconds (default: 30)\n"
                  << "  -M <max_conn>       Max concurrent connections (default: 10000)\n";
    }

    void print_config() const
    {
        std::cout << "=== Server Configuration ===\n"
                  << "Port: " << port << "\n"
                  << "Sub-reactors: " << sub_reactor_count << "\n"
                  << "Root directory: " << root_dir << "\n"
                  << "Database: " << db_host << ":" << db_port << "/" << db_database << "\n"
                  << "Database connections: " << db_min_conn << "-" << db_max_conn << "\n"
                  << "Log: " << log_name << " (" << log_file_size << " bytes, " << log_file_count << " files)\n"
                  << "Idle timeout: " << idle_timeout << " seconds\n"
                  << "Max connections: " << max_connections << "\n"
                  << "============================\n";
    }
};
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include "Reactors/ReactorManager.hpp"

#include "log/Logger.hpp"
#include "log/AsynLogging.hpp"

using namespace Reactors;

std::unique_ptr<ReactorManager> reactor_manager;

// 信号处理函数
void SignalHandler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        std::cout << "\nShutting down server..." << std::endl;
        if (reactor_manager)
        {
            reactor_manager->Stop();
        }
        exit(0);
    }
}

std::string GetRootDirFromArgv0(const char *argv0)
{
    auto exe = std::filesystem::canonical(argv0);
    auto root = exe.parent_path().parent_path() / "static/";
    return root.string();
}

int main(int argc, char *argv[])
{
    // 设置异步日志
    logging::AsynLogging asynLog("ModernWebServer", 1024 * 1024, 3);
    logging::Logger::SetOuput([&asynLog](const std::string &msg)
                          { asynLog.append(msg); });
    logging::Logger::SetFlush([&asynLog]()
                          { asynLog.flush(); });
    logging::Logger::SetLogLevel(logging::LOG_LEVEL::DEBUG);
    asynLog.start();

    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    int port = 7070;
    int sub_reactor_count = std::thread::hardware_concurrency();

    std::string root_dir = GetRootDirFromArgv0(argv[0]);

    // 创建Reactor管理器
    reactor_manager = std::make_unique<ReactorManager>(sub_reactor_count, root_dir);

    // 初始化
    reactor_manager->Initialize();

    // 设置主Reactor监听端口
    if (!reactor_manager->GetMainReactor().SetListenSocket(port))
    {
        std::cerr << "Failed to set up listening socket" << std::endl;
        return 1;
    }

    std::cout << "Server started on port " << port << std::endl;
    std::cout << "Using " << sub_reactor_count << " sub-reactors" << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;

    // 启动所有Reactor
    reactor_manager->Start();

    return 0;
}
#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>
#include "db/mysql_connectionPool.hpp"

using namespace DB;

static std::string EnvOr(const char *key, const char *fallback)
{
    const char *v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

// 测试连接池的单例模式和基本功能
void TestConnectionPoolBasics() {
    std::cout << "=== Testing Connection Pool Basics ===\n";
    
    // 获取单例实例
    auto& pool = MysqlConnectionPool::GetInstance();
    
    try {
        // 初始化连接池（请根据实际数据库配置修改参数）
        pool.Init("localhost", "root", EnvOr("MWS_DB_PASSWORD", "change_me"), "testdb", 3306, 10, 5);
        
        std::cout << "Connection pool initialized successfully!\n";
        std::cout << "Total connections: " << pool.GetConnectionCount() << std::endl;
        std::cout << "Free connections: " << pool.GetFreeConnectionCount() << std::endl;
        
        // 获取连接
        auto conn = pool.GetConnection();
        std::cout << "Got connection, free connections: " << pool.GetFreeConnectionCount() << std::endl;
        
        // 测试自动回收
        conn.reset();
        std::cout << "Connection released, free connections: " << pool.GetFreeConnectionCount() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    std::cout << "\n";
}

// 测试登录校验功能
void TestLoginVerification() {
    std::cout << "=== Testing Login Verification ===\n";
    
    auto& pool = MysqlConnectionPool::GetInstance();
    
    // 测试不存在的用户
    bool result = pool.VerifyLogin("nonexistent", "wrong_password");
    std::cout << "Login for nonexistent user: " << (result ? "SUCCESS" : "FAILED") << " (expected: FAILED)\n";
    
    // 测试错误的密码
    result = pool.VerifyLogin("testuser", "wrong_password");
    std::cout << "Login with wrong password: " << (result ? "SUCCESS" : "FAILED") << " (expected: FAILED)\n";
    
    // 测试正确的登录
    result = pool.VerifyLogin("testuser", EnvOr("MWS_TEST_PASSWORD", "test_password"));
    std::cout << "Login with correct credentials: " << (result ? "SUCCESS" : "FAILED") << " (expected: SUCCESS)\n";
    
    std::cout << "\n";
}

// 测试用户注册功能
void TestUserRegistration() {
    std::cout << "=== Testing User Registration ===\n";
    
    auto& pool = MysqlConnectionPool::GetInstance();
    
    // 生成随机用户名
    static int user_count = 0;
    std::string username = "testuser_" + std::to_string(++user_count);
    std::string password = EnvOr("MWS_TEST_PASSWORD", "test_password");
    
    // 测试注册新用户
    bool result = pool.RegisterUser(username, password);
    std::cout << "Register new user '" << username << "': " << (result ? "SUCCESS" : "FAILED") << " (expected: SUCCESS)\n";
    
    // 测试重复注册
    result = pool.RegisterUser(username, password);
    std::cout << "Register duplicate user '" << username << "': " << (result ? "SUCCESS" : "FAILED") << " (expected: FAILED)\n";
    
    // 测试注册后登录
    result = pool.VerifyLogin(username, password);
    std::cout << "Login after registration: " << (result ? "SUCCESS" : "FAILED") << " (expected: SUCCESS)\n";
    
    std::cout << "\n";
}

// 测试多线程注册
void TestMultiThreadedRegistration() {
    std::cout << "=== Testing Multi-threaded Registration ===\n";
    
    const int num_threads = 10;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([]() {
            auto& pool = MysqlConnectionPool::GetInstance();
            
            static int thread_user_count = 0;
            std::string username = "thread_user_" + std::to_string(++thread_user_count);
            std::string password = EnvOr("MWS_TEST_PASSWORD", "test_password");
            
            bool result = pool.RegisterUser(username, password);
            if (result) {
                std::cout << "Thread " << std::this_thread::get_id() << ": Registered user '" << username << "'\n";
            } else {
                std::cout << "Thread " << std::this_thread::get_id() << ": Failed to register user '" << username << "'\n";
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    std::cout << "\n";
}

int main() {
    // 运行所有测试
    TestConnectionPoolBasics();
    TestLoginVerification();
    TestUserRegistration();
    TestMultiThreadedRegistration();
    
    std::cout << "=== All Tests Completed ===\n";
    
    return 0;
}
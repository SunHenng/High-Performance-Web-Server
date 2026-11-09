add_executable(Server Server.cpp)
target_link_libraries(Server ModernWebServer pthread mysqlclient)


add_executable(testReactors testReactors.cpp)
target_link_libraries(testReactors PRIVATE ModernWebServer)

add_executable(testlog testlog.cpp)
target_link_libraries(testlog PRIVATE ModernWebServer)

add_executable(testMysqlPool testMysqlPool.cpp)
target_link_libraries(testMysqlPool PRIVATE ModernWebServer)

# HTTP 解析器 / 响应构建单元测试。不依赖 MySQL 与网络，
# 全部通过时返回 0，可直接用于 CTest 或 CI。
add_executable(testHttpParser testHttpParser.cpp)
target_link_libraries(testHttpParser PRIVATE ModernWebServer)

add_test(NAME http_unit COMMAND testHttpParser)
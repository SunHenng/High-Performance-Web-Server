# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)

project(ModernWebServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用 CTest：在 build/ 目录执行 `ctest --output-on-failure` 即可运行单元测试
enable_testing()

# 查找依赖
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# 配置可执行文件输出路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 添加 include 目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 查找所有源文件
file(GLOB_RECURSE MODERN_WEBSERVER_SOURCES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# 创建主库
add_library(ModernWebServer STATIC ${MODERN_WEBSERVER_SOURCES})

# 设置库的公共头文件
target_include_directories(ModernWebServer PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 链接依赖库
target_include_directories(ModernWebServer PRIVATE ${MYSQL_INCLUDE_DIRS})
target_link_libraries(ModernWebServer Threads::Threads ${MYSQL_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
target_compile_options(ModernWebServer PRIVATE ${MYSQL_CFLAGS_OTHER})

add_subdirectory(example)


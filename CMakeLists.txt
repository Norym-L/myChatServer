cmake_minimum_required(VERSION 3.20)
project(GateServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 构建开关 ====================
# 三个服务（Gate/Status/Chat）是发布目标；下面几项是可选目标，默认行为保持与本机 CLion 一致
option(MYCHAT_BUILD_TESTS "构建 tests/ 下的单元测试" ON)
option(MYCHAT_BUILD_RESOURCE_SERVER "构建 ResourceServer（WIP：configs/resource.ini 已被删除，默认关）" OFF)
option(MYCHAT_WITH_AWSSDK "查找并链接 AWS SDK（源码里已无 Aws:: 引用，默认关）" OFF)

# ==================== 公共依赖 ====================
set(PLATFORM_LIBS "")
if (WIN32)
    list(APPEND PLATFORM_LIBS ws2_32 mswsock libsodium)
else()
    find_library(SODIUM_LIBRARY NAMES sodium REQUIRED)
    list(APPEND PLATFORM_LIBS ${SODIUM_LIBRARY})
endif()

find_package(Boost REQUIRED COMPONENTS filesystem charconv)
find_package(jsoncpp REQUIRED)
find_package(gRPC REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
# boost::mysql / boost::redis 的头文件实现需要 OpenSSL 与线程库。
# 这两个之前是靠 redis++ 的传递依赖"蹭"进来的，现在 redis++ 已无人使用（全库只有注释提到），
# 必须显式声明，否则 Linux 上链接会缺 SSL_*/pthread 符号。
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(AWSSDK_LINK_LIBRARIES "")
if (MYCHAT_WITH_AWSSDK)
    find_package(AWSSDK REQUIRED COMPONENTS s3)
endif ()

# release 模式不编译 spdlog；必须写在 add_subdirectory 之前，才能作用到各服务子目录
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF)
endif ()

# ==================== 各服务共用的公共代码 ====================
file(GLOB_RECURSE PUBLIC_SOURCE CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/public/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/public/*.h"
)

# 各服务子目录统一引用的变量：公共头文件目录 / 公共链接库
set(MYCHAT_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/public")
set(MYCHAT_COMMON_LIBS
        Boost::boost
        Boost::filesystem
        Boost::charconv
        JsonCpp::JsonCpp
        gRPC::grpc++
        spdlog::spdlog
        fmt::fmt
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ${PLATFORM_LIBS}
        ${AWSSDK_LINK_LIBRARIES}
)

# 把配置文件复制到可执行文件所在目录（三个服务的可执行文件输出到同一目录）
function(mychat_copy_configs TARGET_NAME)
    foreach (CONFIG_FILE ${ARGN})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/configs/${CONFIG_FILE}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${CONFIG_FILE}"
                COMMENT "Copying ${CONFIG_FILE} to output directory"
        )
    endforeach ()
endfunction()

# ==================== 各服务 ====================
add_subdirectory(proto)
add_subdirectory(src/gateServer)
add_subdirectory(src/statusServer)
add_subdirectory(src/chatServer)
if (MYCHAT_BUILD_RESOURCE_SERVER)
    add_subdirectory(src/resourceServer)
endif ()

# ==================== 测试 ====================
if (MYCHAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

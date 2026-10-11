cmake_minimum_required(VERSION 4.3)
project(GateServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
find_package(redis++ REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

file(GLOB_RECURSE PUBLIC_SOURCE CONFIGURE_DEPENDS
        "src/public/*.cpp"
        "src/public/*.h"
)

file(GLOB_RECURSE GATE_SERVER_SOURCES CONFIGURE_DEPENDS
        "src/gateServer/*.cpp"
        "src/gateServer/*.h"
)

file(GLOB_RECURSE STATUS_SERVER_SOURCES CONFIGURE_DEPENDS
        "src/statusServer/*.cpp"
        "src/statusServer/*.h"
)

file(GLOB_RECURSE CHAT_SERVER_SOURCES CONFIGURE_DEPENDS
        "src/chatServer/*.cpp"
        "src/chatServer/*.h"
)

file(GLOB_RECURSE PROTO_FILES CONFIGURE_DEPENDS
        "proto/*.proto"
)

# 手动生成 protobuf 和 gRPC 代码
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

foreach (PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

    # 生成 protobuf 消息
    add_custom_command(
            OUTPUT "${PROTO_OUT_DIR}/${PROTO_NAME}.pb.cc" "${PROTO_OUT_DIR}/${PROTO_NAME}.pb.h"
            COMMAND protobuf::protoc
            --cpp_out=${PROTO_OUT_DIR}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
            ${PROTO_FILE}
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating protobuf for ${PROTO_NAME}"
    )

    # 生成 gRPC 代码
    add_custom_command(
            OUTPUT "${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.cc" "${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.h"
            COMMAND protobuf::protoc
            --grpc_out=${PROTO_OUT_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
            ${PROTO_FILE}
            DEPENDS ${PROTO_FILE} gRPC::grpc_cpp_plugin
            COMMENT "Generating gRPC for ${PROTO_NAME}"
    )

    list(APPEND PROTO_SRCS "${PROTO_OUT_DIR}/${PROTO_NAME}.pb.cc")
    list(APPEND GRPC_SRCS "${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.cc")
endforeach ()

add_executable(GateServer
        ${GATE_SERVER_SOURCES}
        ${PROTO_SRCS} ${GRPC_SRCS}
        ${PUBLIC_SOURCE}
)

add_executable(StatusServer
        ${STATUS_SERVER_SOURCES}
        ${PROTO_SRCS} ${GRPC_SRCS}
        ${PUBLIC_SOURCE}
)

add_executable(ChatServer
        ${CHAT_SERVER_SOURCES}
        ${PROTO_SRCS} ${GRPC_SRCS}
        ${PUBLIC_SOURCE}
)

target_compile_definitions(GateServer PRIVATE
        $<$<CONFIG:Debug>:GATE_DEBUG>
)
target_compile_definitions(StatusServer PRIVATE
        $<$<CONFIG:Debug>:STATUS_DEBUG>
)
target_compile_definitions(ChatServer PRIVATE
        $<$<CONFIG:Debug>:CHAT_DEBUG>
)

# add_test 需要先启用测试支持，否则 ctest 无法发现测试
enable_testing()

add_subdirectory(tests)

target_include_directories(GateServer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/public
        ${PROTO_OUT_DIR}
)

target_include_directories(StatusServer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/public
        ${PROTO_OUT_DIR}
)

target_include_directories(ChatServer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/public
        ${PROTO_OUT_DIR}
)

target_link_libraries(GateServer PRIVATE
        Boost::boost
        Boost::filesystem
        Boost::charconv
        JsonCpp::JsonCpp
        gRPC::grpc++
        redis++::redis++_static
        spdlog::spdlog
        fmt::fmt
        ${PLATFORM_LIBS}
)
target_link_libraries(StatusServer PRIVATE
        Boost::boost
        Boost::filesystem
        Boost::charconv
        JsonCpp::JsonCpp
        gRPC::grpc++
        redis++::redis++_static
        spdlog::spdlog
        fmt::fmt
        ${PLATFORM_LIBS}
)
target_link_libraries(ChatServer PRIVATE
        Boost::boost
        Boost::filesystem
        Boost::charconv
        JsonCpp::JsonCpp
        gRPC::grpc++
        redis++::redis++_static
        spdlog::spdlog
        fmt::fmt
        ${PLATFORM_LIBS}
)
# 配置按服务拆分：构建后全部复制到输出目录，各进程用 --config 指定自己那份
# （三个可执行文件默认输出到同一目录，因此不能像以前那样统一叫 config.ini）
set(CONFIG_FILES
        gate.ini
        status.ini
        chat1.ini
        chat2.ini
)

foreach (TARGET_NAME GateServer StatusServer ChatServer)
    foreach (CONFIG_FILE ${CONFIG_FILES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/configs/${CONFIG_FILE}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${CONFIG_FILE}"
                COMMENT "Copying ${CONFIG_FILE} to output directory"
        )
    endforeach ()
endforeach ()
cmake_minimum_required(VERSION 4.3)
project(GateServer)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(jsoncpp REQUIRED)
find_package(gRPC REQUIRED)
find_package(redis++ REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "src/*.cpp"
        "src/*.h"
)

file(GLOB_RECURSE PROTO_FILES CONFIGURE_DEPENDS
        "proto/*.proto"
)

# 手动生成 protobuf 和 gRPC 代码
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

foreach(PROTO_FILE ${PROTO_FILES})
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
endforeach()

add_executable(GateServer
        ${SOURCES}
        ${PROTO_SRCS} ${GRPC_SRCS}
)

target_include_directories(GateServer PRIVATE ${PROTO_OUT_DIR})

target_link_libraries(GateServer PRIVATE
        Boost::boost
        Boost::filesystem
        JsonCpp::JsonCpp
        gRPC::grpc++
        redis++::redis++_static
        ws2_32
        mswsock
)

# 构建后自动复制 config.ini 到输出目录
add_custom_command(TARGET GateServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:GateServer>/config.ini"
        COMMENT "Copying config.ini to output directory"
)
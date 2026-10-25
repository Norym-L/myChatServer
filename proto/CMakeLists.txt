# 由 proto/*.proto 生成 protobuf 与 gRPC 代码，编译成静态库供各服务链接，
# 避免同一份生成代码在每个服务里重复编译。
file(GLOB_RECURSE PROTO_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.proto"
)

set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

foreach (PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

    # 生成 protobuf 消息
    add_custom_command(
            OUTPUT "${PROTO_OUT_DIR}/${PROTO_NAME}.pb.cc" "${PROTO_OUT_DIR}/${PROTO_NAME}.pb.h"
            COMMAND protobuf::protoc
            --cpp_out=${PROTO_OUT_DIR}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
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
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            ${PROTO_FILE}
            DEPENDS ${PROTO_FILE} gRPC::grpc_cpp_plugin
            COMMENT "Generating gRPC for ${PROTO_NAME}"
    )

    list(APPEND PROTO_SRCS "${PROTO_OUT_DIR}/${PROTO_NAME}.pb.cc")
    list(APPEND GRPC_SRCS "${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.cc")
endforeach ()

add_library(ProtoLib STATIC
        ${PROTO_SRCS}
        ${GRPC_SRCS}
)

# 生成目录对外公开，各服务包含 <message.pb.h> 时无需再加 include 路径
target_include_directories(ProtoLib PUBLIC
        ${PROTO_OUT_DIR}
)

target_link_libraries(ProtoLib PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
)

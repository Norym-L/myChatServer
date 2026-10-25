file(GLOB_RECURSE CHAT_SERVER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_executable(ChatServer
        ${CHAT_SERVER_SOURCES}
        ${PUBLIC_SOURCE}
)

target_compile_definitions(ChatServer PRIVATE
        $<$<CONFIG:Debug>:CHAT_DEBUG>
)

target_include_directories(ChatServer PRIVATE
        ${MYCHAT_PUBLIC_INCLUDE_DIR}
)

target_link_libraries(ChatServer PRIVATE
        ProtoLib
        ${MYCHAT_COMMON_LIBS}
)

# 三个服务的可执行文件仍统一输出到构建目录根部
set_target_properties(ChatServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# chat1.ini / chat2.ini 分别对应两个 ChatServer 进程
mychat_copy_configs(ChatServer chat1.ini chat2.ini)

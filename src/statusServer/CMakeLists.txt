file(GLOB_RECURSE STATUS_SERVER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_executable(StatusServer
        ${STATUS_SERVER_SOURCES}
        ${PUBLIC_SOURCE}
)

target_compile_definitions(StatusServer PRIVATE
        $<$<CONFIG:Debug>:STATUS_DEBUG>
)

target_include_directories(StatusServer PRIVATE
        ${MYCHAT_PUBLIC_INCLUDE_DIR}
)

target_link_libraries(StatusServer PRIVATE
        ProtoLib
        ${MYCHAT_COMMON_LIBS}
)

# 三个服务的可执行文件仍统一输出到构建目录根部
set_target_properties(StatusServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# 只复制本服务需要的配置文件
mychat_copy_configs(StatusServer status.ini)

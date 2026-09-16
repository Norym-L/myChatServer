file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/tests/*.cpp"
)

foreach (TEST_SRC IN LISTS TEST_SOURCES)
    # 用文件名（不含扩展名）作为目标名
    get_filename_component(TEST_NAME "${TEST_SRC}" NAME_WE)

    add_executable(${TEST_NAME} "${TEST_SRC}")

    target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(${TEST_NAME} PRIVATE
            Boost::boost
            Boost::filesystem
            Boost::charconv
            JsonCpp::JsonCpp
            gRPC::grpc++
            redis++::redis++_static
            ws2_32
            mswsock
            libsodium
            spdlog::spdlog
            fmt::fmt
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach ()
cmake_minimum_required(VERSION 4.3)
project(GateServer)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Map Debug/MinSizeRel/RelWithDebInfo to Release for Conan packages
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)

find_package(Boost REQUIRED)
find_package(jsoncpp REQUIRED)

file(GLOB_RECURSE SOURCES
        CONFIGURE_DEPENDS
        "src/*.cpp"
        "src/*.h"
)

add_executable(GateServer main.cpp ${SOURCES})

target_link_libraries(GateServer PRIVATE
    Boost::boost
    JsonCpp::JsonCpp
)

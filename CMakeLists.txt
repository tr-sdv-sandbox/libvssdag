cmake_minimum_required(VERSION 3.14)
project(libVSSDAG VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Find glog
find_package(glog REQUIRED)

# Find Lua
find_package(Lua REQUIRED)

# Find nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, will fetch from GitHub")
    include(FetchContent)
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(json)
endif()

# Find yaml-cpp
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found, will fetch from GitHub")
    include(FetchContent)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# Find dbcppp (DBC parser library)
find_package(dbcppp QUIET)
if(NOT dbcppp_FOUND)
    message(STATUS "dbcppp not found, will try to build from source")
    include(FetchContent)
    FetchContent_Declare(
        dbcppp
        GIT_REPOSITORY https://github.com/xR3b0rn/dbcppp.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(dbcppp)
endif()

# Add include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LUA_INCLUDE_DIR}
)

# Create static library with all the core functionality
add_library(VSSDAG STATIC
    src/can/dbc_parser.cpp
    src/can/can_reader.cpp
    src/lua_mapper.cpp
    src/vss_formatter.cpp
    src/signal_mapper.cpp
    src/signal_dag.cpp
    src/signal_processor_dag.cpp
    src/vss_struct_mapper.cpp
    src/vss_types.cpp
)

# Link library dependencies
target_link_libraries(VSSDAG
    PUBLIC
        glog::glog
        ${LUA_LIBRARIES}
        yaml-cpp
        nlohmann_json::nlohmann_json
        dbcppp
        Threads::Threads
)

# Set include directories for the library
target_include_directories(VSSDAG
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
)

# Add programs subdirectory
add_subdirectory(programs)

# Install target
install(TARGETS VSSDAG
    RUNTIME DESTINATION bin
)

## Install configuration files
#install(DIRECTORY config/
#    DESTINATION etc/can-to-vss
#)


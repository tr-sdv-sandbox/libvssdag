cmake_minimum_required(VERSION 3.14)
project(vssdag VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_INTEGRATION_TESTS "Build integration tests" ON)

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

# Fetch moodycamel concurrent queue
message(STATUS "Fetching moodycamel::ConcurrentQueue")
include(FetchContent)
FetchContent_Declare(
    moodycamel
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.4
)
FetchContent_MakeAvailable(moodycamel)

# Find Google Test if tests are enabled
if(BUILD_TESTS OR BUILD_INTEGRATION_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    include(GoogleTest)
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

# Find or fetch libvss-types
if(NOT TARGET vss::types)
    find_package(vss-types QUIET)
    if(NOT vss-types_FOUND)
        message(STATUS "vss-types not found, will fetch from GitHub")
        include(FetchContent)
        FetchContent_Declare(
            vss-types
            GIT_REPOSITORY https://github.com/tr-sdv-sandbox/libvss-types.git
            GIT_TAG v0.0.1
        )
        FetchContent_MakeAvailable(vss-types)
    endif()
endif()

# Create library with all the core functionality
if(BUILD_SHARED_LIBS)
    add_library(vssdag SHARED)
else()
    add_library(vssdag STATIC)
endif()

target_sources(vssdag
    PRIVATE
        src/can/dbc_parser.cpp
        src/can/can_reader.cpp
        src/can/can_source.cpp
        src/lua_mapper.cpp
        src/vss_formatter.cpp
        src/signal_dag.cpp
        src/signal_processor.cpp
        src/vss_struct_mapper.cpp
        src/vss_types.cpp
)

# Set include directories for the library
target_include_directories(vssdag
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${LUA_INCLUDE_DIR}
        ${moodycamel_SOURCE_DIR}
)

# Link library dependencies
target_link_libraries(vssdag
    PUBLIC
        glog::glog
        ${LUA_LIBRARIES}
        yaml-cpp
        nlohmann_json::nlohmann_json
        dbcppp
        vss::types
        Threads::Threads
)

# Add subdirectories
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
include(GNUInstallDirs)

# Set the export name to create vssdag::vssdag target (must be before install)
set_target_properties(vssdag PROPERTIES EXPORT_NAME vssdag)

# Install library
install(TARGETS vssdag
    EXPORT vssdag-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/vssdag
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install CMake config files
install(EXPORT vssdag-targets
    FILE vssdagTargets.cmake
    NAMESPACE vssdag::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vssdag
)

# Create and install package config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vssdagConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vssdagConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vssdag
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/vssdagConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/vssdagConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/vssdagConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vssdag
)


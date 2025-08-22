cmake_minimum_required(VERSION 3.14)

# Set test binary output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

# Unit tests
if(BUILD_TESTS)
    add_subdirectory(unit)
endif()

# Integration tests
if(BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()

# Copy test data to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data 
     DESTINATION ${CMAKE_BINARY_DIR}/tests)
add_library(vssdag_test_utils INTERFACE)

target_include_directories(vssdag_test_utils INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vssdag_test_utils INTERFACE
    vssdag
)
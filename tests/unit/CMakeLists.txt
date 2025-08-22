cmake_minimum_required(VERSION 3.14)

# Create a test executable for each component

# Test for SignalDAG
add_executable(test_signal_dag
    test_signal_dag.cpp
)
target_link_libraries(test_signal_dag
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_signal_dag)

# Test for VSSTypes
add_executable(test_vss_types
    test_vss_types.cpp
)
target_link_libraries(test_vss_types
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_vss_types)

# Test for LuaMapper (simplified version that matches actual API)
add_executable(test_lua_mapper_simple
    test_lua_mapper_simple.cpp
)
target_link_libraries(test_lua_mapper_simple
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_lua_mapper_simple)

# TODO: Fix these tests to match actual API
# # Test for LuaMapper (full version - needs API updates)
# add_executable(test_lua_mapper
#     test_lua_mapper.cpp
# )
# target_link_libraries(test_lua_mapper
#     vssdag
#     GTest::gtest
#     GTest::gtest_main
# )
# gtest_discover_tests(test_lua_mapper)

# Test for DBCParser
add_executable(test_dbc_parser
    test_dbc_parser.cpp
)
target_link_libraries(test_dbc_parser
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_dbc_parser)

# Test for SignalProcessor
add_executable(test_signal_processor
    test_signal_processor.cpp
)
target_link_libraries(test_signal_processor
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_signal_processor)
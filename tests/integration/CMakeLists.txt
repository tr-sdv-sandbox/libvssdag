cmake_minimum_required(VERSION 3.14)

# Integration test for CAN log replay
add_executable(test_can_replay
    test_can_replay.cpp
)
target_link_libraries(test_can_replay
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_can_replay)

# Integration test for end-to-end processing
add_executable(test_end_to_end
    test_end_to_end.cpp
)
target_link_libraries(test_end_to_end
    vssdag
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_end_to_end)
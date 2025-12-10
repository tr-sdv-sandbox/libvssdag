add_executable(avtp_test_sender main.cpp)

target_link_libraries(avtp_test_sender
    PRIVATE
        vssdag
        glog::glog
)

install(TARGETS avtp_test_sender
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

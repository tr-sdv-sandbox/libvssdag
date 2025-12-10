add_executable(avtp_canplayer main.cpp)

target_link_libraries(avtp_canplayer
    PRIVATE
        vssdag
        glog::glog
)

install(TARGETS avtp_canplayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

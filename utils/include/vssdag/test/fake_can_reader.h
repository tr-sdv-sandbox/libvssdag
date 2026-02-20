#pragma once

#include "vssdag/can/can_reader.h"

namespace vssdag::utils {

class FakeCANReader : public CANReader {
public:
    bool open(const std::string& interface) override { return true; }
    void close() override {}
    bool is_open() const override { return true; }
    void read_loop() override {}
    void stop() override {}
    
    void inject_frame(const CANFrame& frame) {
        if (frame_handler_) frame_handler_(frame);
    }
};

} // namespace vssdag::test
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace vssdag {

struct CANFrame {
    uint32_t id;
    std::vector<uint8_t> data;
    uint64_t timestamp_us;
};

class CANReader {
public:
    using FrameHandler = std::function<void(const CANFrame&)>;
    
    CANReader() = default;
    virtual ~CANReader() = default;
    
    virtual bool open(const std::string& interface) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    
    void set_frame_handler(FrameHandler handler) { frame_handler_ = handler; }
    
    virtual void read_loop() = 0;
    virtual void stop() = 0;

protected:
    FrameHandler frame_handler_;
};

class SocketCANReader : public CANReader {
public:
    SocketCANReader();
    ~SocketCANReader() override;
    
    bool open(const std::string& interface) override;
    void close() override;
    bool is_open() const override;
    
    void read_loop() override;
    void stop() override;

private:
    int socket_fd_ = -1;
    bool should_stop_ = false;
    std::string interface_name_;
};

} // namespace vssdag
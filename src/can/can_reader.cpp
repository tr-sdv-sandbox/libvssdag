#include "vssdag/can/can_reader.h"
#include <glog/logging.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace vssdag {

SocketCANReader::SocketCANReader() {
}

SocketCANReader::~SocketCANReader() {
    close();
}

bool SocketCANReader::open(const std::string& interface) {
    socket_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd_ < 0) {
        LOG(ERROR) << "Failed to create CAN socket: " << strerror(errno);
        return false;
    }
    
    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    
    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        LOG(ERROR) << "Failed to get interface index for " << interface << ": " << strerror(errno);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG(ERROR) << "Failed to bind CAN socket: " << strerror(errno);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    interface_name_ = interface;
    LOG(INFO) << "Opened CAN interface: " << interface;
    return true;
}

void SocketCANReader::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        LOG(INFO) << "Closed CAN interface: " << interface_name_;
    }
}

bool SocketCANReader::is_open() const {
    return socket_fd_ >= 0;
}

void SocketCANReader::read_loop() {
    if (!is_open()) {
        LOG(ERROR) << "CAN socket not open";
        return;
    }
    
    should_stop_ = false;
    struct can_frame frame;
    
    while (!should_stop_) {
        ssize_t nbytes = read(socket_fd_, &frame, sizeof(struct can_frame));
        
        if (nbytes < 0) {
            if (errno != EINTR) {
                LOG(ERROR) << "Error reading from CAN socket: " << strerror(errno);
            }
            continue;
        }
        
        if (nbytes < sizeof(struct can_frame)) {
            LOG(WARNING) << "Incomplete CAN frame received";
            continue;
        }
        
        if (frame_handler_) {
            CANFrame can_frame;
            can_frame.id = frame.can_id & CAN_EFF_MASK;
            can_frame.data.assign(frame.data, frame.data + frame.can_dlc);
            
            auto now = std::chrono::steady_clock::now();
            can_frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            
            frame_handler_(can_frame);
        }
    }
}

void SocketCANReader::stop() {
    should_stop_ = true;
}

} // namespace vssdag
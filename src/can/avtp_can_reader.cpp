#include "vssdag/can/avtp_can_reader.h"

#include <glog/logging.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <arpa/inet.h>

namespace vssdag {

// IEEE 1722 AVTP header offsets and constants
namespace avtp {
    // Common AVTP header (first 12 bytes after Ethernet header)
    constexpr size_t HEADER_SIZE = 12;

    // AVTP subtypes
    constexpr uint8_t SUBTYPE_ACF = 0x7F;  // AVTP Control Format

    // ACF message types
    constexpr uint8_t ACF_TYPE_CAN = 0x02;        // CAN message
    constexpr uint8_t ACF_TYPE_CAN_BRIEF = 0x03;  // CAN Brief message

    // ACF CAN header size (after AVTP common header)
    constexpr size_t ACF_CAN_HEADER_SIZE = 4;

    // Maximum CAN data length
    constexpr size_t CAN_MAX_DLEN = 8;
    constexpr size_t CANFD_MAX_DLEN = 64;
}

AVTPCanReader::AVTPCanReader() = default;

AVTPCanReader::AVTPCanReader(const Config& config) : config_(config) {}

AVTPCanReader::~AVTPCanReader() {
    close();
}

bool AVTPCanReader::open(const std::string& interface) {
    if (socket_fd_ >= 0) {
        LOG(WARNING) << "AVTPCanReader already open";
        return false;
    }

    interface_name_ = interface;

    // Create raw socket for IEEE 1722 (ethertype 0x22F0)
    socket_fd_ = socket(AF_PACKET, SOCK_RAW, htons(config_.ethertype));
    if (socket_fd_ < 0) {
        LOG(ERROR) << "Failed to create raw socket: " << strerror(errno);
        LOG(ERROR) << "Hint: Raw sockets require CAP_NET_RAW capability or root";
        return false;
    }

    // Get interface index
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        LOG(ERROR) << "Failed to get interface index for " << interface << ": " << strerror(errno);
        close();
        return false;
    }

    // Bind to interface
    struct sockaddr_ll sll;
    std::memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(config_.ethertype);
    sll.sll_ifindex = ifr.ifr_ifindex;

    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll)) < 0) {
        LOG(ERROR) << "Failed to bind socket to " << interface << ": " << strerror(errno);
        close();
        return false;
    }

    // Enable promiscuous mode to receive all AVTP packets
    struct packet_mreq mreq;
    std::memset(&mreq, 0, sizeof(mreq));
    mreq.mr_ifindex = ifr.ifr_ifindex;
    mreq.mr_type = PACKET_MR_PROMISC;

    if (setsockopt(socket_fd_, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG(WARNING) << "Failed to enable promiscuous mode: " << strerror(errno);
        // Continue anyway, might still work for unicast
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    should_stop_ = false;
    stats_ = Stats{};

    LOG(INFO) << "AVTPCanReader opened on " << interface << " (ethertype 0x"
              << std::hex << config_.ethertype << std::dec << ")";

    return true;
}

void AVTPCanReader::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        LOG(INFO) << "AVTPCanReader closed: " << stats_.packets_received << " packets, "
                  << stats_.can_frames_extracted << " CAN frames";
    }
}

bool AVTPCanReader::is_open() const {
    return socket_fd_ >= 0;
}

void AVTPCanReader::stop() {
    should_stop_ = true;
}

void AVTPCanReader::read_loop() {
    if (socket_fd_ < 0) {
        LOG(ERROR) << "AVTPCanReader not open";
        return;
    }

    constexpr size_t BUFFER_SIZE = 2048;
    uint8_t buffer[BUFFER_SIZE];

    LOG(INFO) << "AVTPCanReader read loop started on " << interface_name_;

    while (!should_stop_) {
        ssize_t len = recv(socket_fd_, buffer, BUFFER_SIZE, 0);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout, check should_stop_ and continue
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            LOG(ERROR) << "recv() failed: " << strerror(errno);
            break;
        }

        if (len == 0) {
            continue;
        }

        stats_.packets_received++;

        // Skip Ethernet header (14 bytes: 6 dst + 6 src + 2 ethertype)
        // Note: ethertype is already filtered by the socket
        constexpr size_t ETH_HEADER_SIZE = 14;
        if (static_cast<size_t>(len) < ETH_HEADER_SIZE + avtp::HEADER_SIZE) {
            stats_.invalid_packets++;
            continue;
        }

        // Parse AVTP payload
        parse_avtp_packet(buffer + ETH_HEADER_SIZE, len - ETH_HEADER_SIZE);
    }

    LOG(INFO) << "AVTPCanReader read loop stopped";
}

int AVTPCanReader::parse_avtp_packet(const uint8_t* data, size_t len) {
    if (len < avtp::HEADER_SIZE) {
        stats_.invalid_packets++;
        return 0;
    }

    // AVTP common header format:
    // Byte 0: subtype
    // Byte 1: sv(1) + version(3) + mr(1) + r(1) + gv(1) + tv(1)
    // Bytes 2-3: sequence_num
    // Bytes 4-11: stream_id (8 bytes)

    uint8_t subtype = data[0];

    // Check stream ID filter if enabled
    if (config_.filter_stream_id) {
        if (std::memcmp(data + 4, config_.stream_id, 8) != 0) {
            stats_.filtered_packets++;
            return 0;
        }
    }

    // We're looking for ACF subtype (0x7F)
    if (subtype != avtp::SUBTYPE_ACF) {
        // Not ACF, could be audio/video stream - ignore
        return 0;
    }

    // ACF header follows common header
    // ACF format:
    // Byte 12: acf_msg_type (upper 7 bits) + acf_msg_length MSB (lower 1 bit)
    // Byte 13: acf_msg_length (lower 8 bits)
    // Bytes 14+: ACF message data

    size_t offset = avtp::HEADER_SIZE;
    int frames_extracted = 0;

    while (offset + 2 <= len) {
        uint8_t acf_type = data[offset] >> 1;
        uint16_t acf_len = ((data[offset] & 0x01) << 8) | data[offset + 1];

        // ACF length is in quadlets (4-byte units)
        size_t msg_len = acf_len * 4;
        offset += 2;

        if (offset + msg_len > len) {
            stats_.invalid_packets++;
            break;
        }

        // Check for CAN or CAN Brief message types
        if (acf_type == avtp::ACF_TYPE_CAN || acf_type == avtp::ACF_TYPE_CAN_BRIEF) {
            CANFrame frame;
            if (parse_acf_can(data + offset, msg_len, frame)) {
                // Set timestamp
                auto now = std::chrono::steady_clock::now();
                frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    now.time_since_epoch()).count();

                if (frame_handler_) {
                    frame_handler_(frame);
                }
                frames_extracted++;
                stats_.can_frames_extracted++;
            }
        }

        offset += msg_len;
    }

    return frames_extracted;
}

bool AVTPCanReader::parse_acf_can(const uint8_t* data, size_t len, CANFrame& frame) {
    // ACF CAN message format (IEEE 1722-2016):
    // Byte 0: pad(2) + mtv(1) + rtr(1) + eff(1) + brs(1) + fdf(1) + r(1)
    // Byte 1: rsv(8) or upper CAN ID bits for 29-bit
    // Byte 2-3: CAN ID (lower 16 bits) or message timestamp
    // Byte 4: rsv(4) + dlc(4)
    // Bytes 5-7: padding
    // Bytes 8+: CAN data

    if (len < 8) {
        return false;
    }

    uint8_t flags = data[0];
    bool eff = (flags >> 3) & 0x01;  // Extended frame format (29-bit ID)
    bool rtr = (flags >> 4) & 0x01;  // Remote transmission request

    // Extract CAN ID
    if (eff) {
        // 29-bit extended ID
        frame.id = ((data[1] & 0x1F) << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
        frame.id |= 0x80000000;  // Set EFF flag in ID
    } else {
        // 11-bit standard ID
        frame.id = (data[2] << 8) | data[3];
    }

    if (rtr) {
        frame.id |= 0x40000000;  // Set RTR flag in ID
    }

    // Extract DLC (data length code)
    uint8_t dlc = data[4] & 0x0F;
    size_t data_len = dlc;

    // CAN FD can have more than 8 bytes
    if (dlc > 8 && dlc <= 15) {
        // CAN FD DLC encoding
        static const size_t dlc_to_len[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
        data_len = dlc_to_len[dlc];
    }

    // Validate we have enough data
    if (len < 8 + data_len) {
        return false;
    }

    // Extract CAN data
    frame.data.assign(data + 8, data + 8 + data_len);

    return true;
}

} // namespace vssdag

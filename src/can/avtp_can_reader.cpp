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

// Open1722 headers
#include <avtp/CommonHeader.h>
#include <avtp/acf/Can.h>
#include <avtp/acf/AcfCommon.h>
#include <avtp/acf/Ntscf.h>
#include <avtp/acf/Tscf.h>

namespace vssdag {

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
        if (static_cast<size_t>(len) < ETH_HEADER_SIZE + AVTP_COMMON_HEADER_LEN) {
            stats_.invalid_packets++;
            continue;
        }

        // Parse AVTP payload
        parse_avtp_packet(buffer + ETH_HEADER_SIZE, len - ETH_HEADER_SIZE);
    }

    LOG(INFO) << "AVTPCanReader read loop stopped";
}

int AVTPCanReader::parse_avtp_packet(const uint8_t* data, size_t len) {
    if (len < AVTP_COMMON_HEADER_LEN) {
        stats_.invalid_packets++;
        return 0;
    }

    // Get AVTP subtype from common header
    Avtp_CommonHeader_t* common_header = (Avtp_CommonHeader_t*)data;
    uint8_t subtype = Avtp_CommonHeader_GetSubtype(common_header);

    int frames_extracted = 0;

    // Handle TSCF (Time-Synchronous Control Format) - subtype 0x00
    if (subtype == AVTP_SUBTYPE_TSCF) {
        if (len < AVTP_TSCF_HEADER_LEN) {
            stats_.invalid_packets++;
            return 0;
        }

        Avtp_Tscf_t* tscf = (Avtp_Tscf_t*)data;

        // Check stream ID filter if enabled
        if (config_.filter_stream_id) {
            uint64_t stream_id = Avtp_Tscf_GetStreamId(tscf);
            uint64_t config_stream_id = 0;
            std::memcpy(&config_stream_id, config_.stream_id, 8);
            if (stream_id != config_stream_id) {
                stats_.filtered_packets++;
                return 0;
            }
        }

        // Get payload length and offset
        uint16_t tscf_data_len = Avtp_Tscf_GetStreamDataLength(tscf);
        const uint8_t* acf_data = data + AVTP_TSCF_HEADER_LEN;
        size_t acf_remaining = std::min(static_cast<size_t>(tscf_data_len),
                                        len - AVTP_TSCF_HEADER_LEN);

        // Parse ACF messages within TSCF
        frames_extracted = parse_acf_messages(acf_data, acf_remaining);
    }
    // Handle NTSCF (Non-Time-Synchronous Control Format) - subtype 0x82
    else if (subtype == AVTP_SUBTYPE_NTSCF) {
        if (len < AVTP_NTSCF_HEADER_LEN) {
            stats_.invalid_packets++;
            return 0;
        }

        Avtp_Ntscf_t* ntscf = (Avtp_Ntscf_t*)data;

        // Check stream ID filter if enabled
        if (config_.filter_stream_id) {
            uint64_t stream_id = Avtp_Ntscf_GetStreamId(ntscf);
            uint64_t config_stream_id = 0;
            std::memcpy(&config_stream_id, config_.stream_id, 8);
            if (stream_id != config_stream_id) {
                stats_.filtered_packets++;
                return 0;
            }
        }

        // Get payload length and offset
        uint16_t ntscf_data_len = Avtp_Ntscf_GetNtscfDataLength(ntscf);
        const uint8_t* acf_data = data + AVTP_NTSCF_HEADER_LEN;
        size_t acf_remaining = std::min(static_cast<size_t>(ntscf_data_len),
                                        len - AVTP_NTSCF_HEADER_LEN);

        // Parse ACF messages within NTSCF
        frames_extracted = parse_acf_messages(acf_data, acf_remaining);
    }
    else {
        // Unknown subtype, ignore
        VLOG(2) << "Unknown AVTP subtype: 0x" << std::hex << static_cast<int>(subtype);
    }

    return frames_extracted;
}

int AVTPCanReader::parse_acf_messages(const uint8_t* data, size_t len) {
    int frames_extracted = 0;
    size_t offset = 0;

    while (offset + AVTP_ACF_COMMON_HEADER_LEN <= len) {
        // Get ACF message type and length from common ACF header
        Avtp_AcfCommon_t* acf_common = (Avtp_AcfCommon_t*)(data + offset);
        uint8_t acf_msg_type = Avtp_AcfCommon_GetAcfMsgType(acf_common);
        uint16_t acf_msg_length = Avtp_AcfCommon_GetAcfMsgLength(acf_common);

        // Length is in quadlets (4-byte units)
        size_t msg_len = acf_msg_length * AVTP_QUADLET_SIZE;

        if (offset + msg_len > len) {
            stats_.invalid_packets++;
            break;
        }

        // Check for ACF CAN message types
        if (acf_msg_type == AVTP_ACF_TYPE_CAN || acf_msg_type == AVTP_ACF_TYPE_CAN_BRIEF) {
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
    if (len < AVTP_CAN_HEADER_LEN) {
        return false;
    }

    const Avtp_Can_t* can_pdu = reinterpret_cast<const Avtp_Can_t*>(data);

    // Validate the frame using Open1722
    if (!Avtp_Can_IsValid(can_pdu, len)) {
        return false;
    }

    // Get CAN message details using Open1722 API
    uint32_t can_id = Avtp_Can_GetCanIdentifier(can_pdu);
    uint8_t eff = Avtp_Can_GetEff(can_pdu);
    uint8_t rtr = Avtp_Can_GetRtr(can_pdu);

    // Set CAN ID with flags
    frame.id = can_id;
    if (eff) {
        frame.id |= 0x80000000;  // Extended frame format flag
    }
    if (rtr) {
        frame.id |= 0x40000000;  // Remote transmission request flag
    }

    // Get payload using Open1722 API
    uint8_t payload_len = Avtp_Can_GetCanPayloadLength(can_pdu);
    if (payload_len > 64) {
        payload_len = 64;  // CAN-FD max
    }

    if (payload_len > 0) {
        const uint8_t* payload_ptr = Avtp_Can_GetPayload(can_pdu);
        frame.data.assign(payload_ptr, payload_ptr + payload_len);
    } else {
        frame.data.clear();
    }

    return true;
}

} // namespace vssdag

#include "vssdag/can/avtp_can_sender.h"

#include <glog/logging.h>
#include <iomanip>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

// Open1722 headers
#include <avtp/CommonHeader.h>
#include <avtp/acf/Can.h>
#include <avtp/acf/AcfCommon.h>
#include <avtp/acf/Ntscf.h>

namespace vssdag {

AVTPCanSender::AVTPCanSender() = default;

AVTPCanSender::AVTPCanSender(const Config& config) : config_(config) {}

AVTPCanSender::~AVTPCanSender() {
    close();
}

bool AVTPCanSender::open(const std::string& interface) {
    if (socket_fd_ >= 0) {
        LOG(WARNING) << "AVTPCanSender already open";
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

    // Get interface index and MAC address
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        LOG(ERROR) << "Failed to get interface index for " << interface << ": " << strerror(errno);
        close();
        return false;
    }
    if_index_ = ifr.ifr_ifindex;

    // Get interface MAC address if not configured
    bool need_mac = true;
    for (int i = 0; i < 6; i++) {
        if (config_.src_mac[i] != 0) {
            need_mac = false;
            break;
        }
    }

    if (need_mac) {
        if (ioctl(socket_fd_, SIOCGIFHWADDR, &ifr) < 0) {
            LOG(WARNING) << "Failed to get interface MAC: " << strerror(errno);
        } else {
            std::memcpy(config_.src_mac, ifr.ifr_hwaddr.sa_data, 6);
        }
    }

    stats_ = Stats{};

    LOG(INFO) << "AVTPCanSender opened on " << interface
              << " (index " << if_index_ << ", MAC "
              << std::hex << std::setfill('0')
              << std::setw(2) << static_cast<int>(config_.src_mac[0]) << ":"
              << std::setw(2) << static_cast<int>(config_.src_mac[1]) << ":"
              << std::setw(2) << static_cast<int>(config_.src_mac[2]) << ":"
              << std::setw(2) << static_cast<int>(config_.src_mac[3]) << ":"
              << std::setw(2) << static_cast<int>(config_.src_mac[4]) << ":"
              << std::setw(2) << static_cast<int>(config_.src_mac[5])
              << std::dec << ")";

    return true;
}

void AVTPCanSender::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        LOG(INFO) << "AVTPCanSender closed: " << stats_.packets_sent << " packets, "
                  << stats_.can_frames_sent << " CAN frames sent";
    }
}

bool AVTPCanSender::is_open() const {
    return socket_fd_ >= 0;
}

void AVTPCanSender::set_stream_id(const uint8_t stream_id[8]) {
    std::memcpy(config_.stream_id, stream_id, 8);
}

void AVTPCanSender::set_dst_mac(const uint8_t mac[6]) {
    std::memcpy(config_.dst_mac, mac, 6);
}

bool AVTPCanSender::send(const CANFrame& frame) {
    std::vector<CANFrame> frames = {frame};
    return send_batch(frames) == 1;
}

int AVTPCanSender::send_batch(const std::vector<CANFrame>& frames) {
    if (socket_fd_ < 0) {
        LOG(ERROR) << "AVTPCanSender not open";
        return 0;
    }

    if (frames.empty()) {
        return 0;
    }

    // Build Ethernet frame with AVTP payload
    constexpr size_t MAX_FRAME_SIZE = 1522;
    uint8_t buffer[MAX_FRAME_SIZE];

    size_t frame_size = build_ethernet_frame(frames, buffer, MAX_FRAME_SIZE);
    if (frame_size == 0) {
        stats_.send_errors++;
        return 0;
    }

    // Set up destination address
    struct sockaddr_ll dest_addr;
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sll_family = AF_PACKET;
    dest_addr.sll_protocol = htons(config_.ethertype);
    dest_addr.sll_ifindex = if_index_;
    dest_addr.sll_halen = 6;
    std::memcpy(dest_addr.sll_addr, config_.dst_mac, 6);

    // Send the frame
    ssize_t sent = sendto(socket_fd_, buffer, frame_size, 0,
                          reinterpret_cast<struct sockaddr*>(&dest_addr),
                          sizeof(dest_addr));

    if (sent < 0) {
        LOG(ERROR) << "sendto() failed: " << strerror(errno);
        stats_.send_errors++;
        return 0;
    }

    stats_.packets_sent++;
    stats_.can_frames_sent += frames.size();
    stats_.sequence_num++;

    return static_cast<int>(frames.size());
}

size_t AVTPCanSender::build_ethernet_frame(const std::vector<CANFrame>& frames,
                                           uint8_t* buffer, size_t buffer_size) {
    constexpr size_t ETH_HEADER_SIZE = 14;
    constexpr size_t MAX_ACF_SIZE = 24 + 64;  // ACF CAN header + max payload

    if (buffer_size < ETH_HEADER_SIZE + AVTP_NTSCF_HEADER_LEN) {
        return 0;
    }

    // Build Ethernet header
    std::memcpy(buffer, config_.dst_mac, 6);           // Destination MAC
    std::memcpy(buffer + 6, config_.src_mac, 6);       // Source MAC
    buffer[12] = (config_.ethertype >> 8) & 0xFF;     // Ethertype high
    buffer[13] = config_.ethertype & 0xFF;            // Ethertype low

    uint8_t* avtp_start = buffer + ETH_HEADER_SIZE;
    size_t avtp_offset = 0;

    // Initialize NTSCF header using Open1722
    Avtp_Ntscf_t* ntscf = reinterpret_cast<Avtp_Ntscf_t*>(avtp_start);
    std::memset(ntscf, 0, AVTP_NTSCF_HEADER_LEN);

    Avtp_Ntscf_Init(ntscf);
    Avtp_Ntscf_SetSequenceNum(ntscf, stats_.sequence_num);

    // Set stream ID
    uint64_t stream_id_val = 0;
    std::memcpy(&stream_id_val, config_.stream_id, 8);
    Avtp_Ntscf_SetStreamId(ntscf, stream_id_val);

    avtp_offset = AVTP_NTSCF_HEADER_LEN;

    // Build ACF CAN messages for each frame
    size_t acf_total_len = 0;
    for (const auto& can_frame : frames) {
        if (avtp_offset + MAX_ACF_SIZE > buffer_size - ETH_HEADER_SIZE) {
            LOG(WARNING) << "Buffer too small for all CAN frames";
            break;
        }

        uint8_t* acf_start = avtp_start + avtp_offset;

        // Initialize ACF CAN using Open1722
        Avtp_Can_t* acf_can = reinterpret_cast<Avtp_Can_t*>(acf_start);
        std::memset(acf_can, 0, AVTP_CAN_HEADER_LEN);

        Avtp_Can_Init(acf_can);

        // Set CAN bus ID
        Avtp_Can_SetCanBusId(acf_can, config_.can_bus_id);

        // Extract CAN ID and flags
        uint32_t can_id = can_frame.id & 0x1FFFFFFF;  // 29-bit mask
        bool is_extended = (can_frame.id & 0x80000000) != 0;
        bool is_rtr = (can_frame.id & 0x40000000) != 0;

        Avtp_Can_SetCanIdentifier(acf_can, can_id);

        // Set flags
        if (is_extended) {
            Avtp_Can_EnableEff(acf_can);
        }
        if (is_rtr) {
            Avtp_Can_EnableRtr(acf_can);
        }

        // Set payload
        uint8_t payload_len = static_cast<uint8_t>(can_frame.data.size());
        if (payload_len > 64) {
            payload_len = 64;  // CAN-FD max
        }

        if (payload_len > 0) {
            Avtp_Can_SetPayload(acf_can, const_cast<uint8_t*>(can_frame.data.data()), payload_len);
        }

        // Finalize the ACF CAN message (sets length fields and padding)
        Avtp_Can_Finalize(acf_can, payload_len);

        // Get the actual message length
        uint16_t acf_msg_len = Avtp_Can_GetAcfMsgLength(acf_can);
        size_t acf_size = acf_msg_len * AVTP_QUADLET_SIZE;

        avtp_offset += acf_size;
        acf_total_len += acf_size;
    }

    // Update NTSCF data length
    Avtp_Ntscf_SetNtscfDataLength(ntscf, static_cast<uint16_t>(acf_total_len));

    return ETH_HEADER_SIZE + avtp_offset;
}

} // namespace vssdag

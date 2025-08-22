#include "vssdag/vss_formatter.h"
#include <glog/logging.h>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace vssdag {

void VSSFormatter::log_vss_signal(const VSSSignal& signal) {
    LOG(INFO) << format_vss_signal(signal);
}

std::string VSSFormatter::format_vss_signal(const VSSSignal& signal) {
    std::ostringstream oss;
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Format: [timestamp] VSS: path = value (type)
    oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    oss << "VSS: " << signal.path << " = " << signal.value;
    oss << " (" << signal.value_type << ")";
    
    return oss.str();
}

} // namespace vssdag
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

    // Use timestamp from the signal's qualified_value
    auto time_t = std::chrono::system_clock::to_time_t(signal.qualified_value.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        signal.qualified_value.timestamp.time_since_epoch()) % 1000;

    // Format: [timestamp] VSS: path = value (type) [quality]
    oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    oss << "VSS: " << signal.path << " = ";
    oss << VSSTypeHelper::to_string(signal.qualified_value.value);
    oss << " [" << signal_quality_to_string(signal.qualified_value.quality) << "]";

    return oss.str();
}

} // namespace vssdag
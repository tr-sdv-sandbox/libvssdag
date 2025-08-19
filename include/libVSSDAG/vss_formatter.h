#pragma once

#include <string>
#include "lua_mapper.h"

namespace can_to_vss {

class VSSFormatter {
public:
    static void log_vss_signal(const VSSSignal& signal);
    static std::string format_vss_signal(const VSSSignal& signal);
};

} // namespace can_to_vss
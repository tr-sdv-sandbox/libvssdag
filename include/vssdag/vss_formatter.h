#pragma once

#include <string>
#include "vssdag/lua_mapper.h"

namespace vssdag {

class VSSFormatter {
public:
    static void log_vss_signal(const VSSSignal& signal);
    static std::string format_vss_signal(const VSSSignal& signal);
};

} // namespace vssdag
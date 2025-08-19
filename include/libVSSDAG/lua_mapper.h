#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include "vss_types.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace can_to_vss {

struct VSSSignal {
    std::string path;
    std::string value_type;
    std::string value;
};

class LuaMapper {
public:
    LuaMapper();
    ~LuaMapper();
    
    bool load_mapping_file(const std::string& lua_file);
    
    std::vector<VSSSignal> map_can_signals(const std::vector<std::pair<std::string, double>>& can_signals);
    
    void set_can_signal_value(const std::string& signal_name, double value);
    
    // New methods for VSS mapper
    bool execute_lua_string(const std::string& lua_code);
    std::optional<VSSSignal> call_transform_function(const std::string& signal_name, double value);
    
    // Get a Lua variable value (for debugging/testing)
    std::optional<std::string> get_lua_variable(const std::string& var_name);
    
    // Get the Lua state for advanced operations
    lua_State* get_lua_state() { return L_; }

private:
    lua_State* L_ = nullptr;
    
    bool execute_mapping_function();
    VSSSignal extract_vss_signal(int index);
    std::optional<VSSSignal> extract_vss_signal_from_stack();
};

} // namespace can_to_vss
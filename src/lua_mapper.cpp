#include "vssdag/lua_mapper.h"
#include <glog/logging.h>
#include <sstream>
#include <optional>

namespace vssdag {

LuaMapper::LuaMapper() {
    L_ = luaL_newstate();
    if (!L_) {
        LOG(ERROR) << "Failed to create Lua state";
        return;
    }
    
    luaL_openlibs(L_);
    
    // Create global CAN signals table
    lua_newtable(L_);
    lua_setglobal(L_, "can_signals");
    
    // Create global VSS output table
    lua_newtable(L_);
    lua_setglobal(L_, "vss_signals");
}

LuaMapper::~LuaMapper() {
    if (L_) {
        lua_close(L_);
    }
}

bool LuaMapper::load_mapping_file(const std::string& lua_file) {
    if (!L_) {
        LOG(ERROR) << "Lua state not initialized";
        return false;
    }
    
    if (luaL_dofile(L_, lua_file.c_str()) != LUA_OK) {
        LOG(ERROR) << "Failed to load Lua file: " << lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    
    // Check if map_signals function exists
    lua_getglobal(L_, "map_signals");
    if (!lua_isfunction(L_, -1)) {
        LOG(ERROR) << "map_signals function not found in Lua file";
        lua_pop(L_, 1);
        return false;
    }
    lua_pop(L_, 1);
    
    LOG(INFO) << "Successfully loaded Lua mapping file: " << lua_file;
    return true;
}

void LuaMapper::set_can_signal_value(const std::string& signal_name, double value) {
    if (!L_) return;
    
    lua_getglobal(L_, "can_signals");
    lua_pushnumber(L_, value);
    lua_setfield(L_, -2, signal_name.c_str());
    lua_pop(L_, 1);
}

std::vector<VSSSignal> LuaMapper::map_can_signals(const std::vector<std::pair<std::string, double>>& can_signals) {
    std::vector<VSSSignal> vss_signals;
    
    if (!L_) {
        LOG(ERROR) << "Lua state not initialized";
        return vss_signals;
    }
    
    // Clear VSS signals table
    lua_newtable(L_);
    lua_setglobal(L_, "vss_signals");
    
    // Update CAN signals table
    for (const auto& [name, value] : can_signals) {
        set_can_signal_value(name, value);
    }
    
    // Execute mapping function
    if (!execute_mapping_function()) {
        return vss_signals;
    }
    
    // Extract VSS signals from the global table
    lua_getglobal(L_, "vss_signals");
    if (!lua_istable(L_, -1)) {
        LOG(ERROR) << "vss_signals is not a table";
        lua_pop(L_, 1);
        return vss_signals;
    }
    
    // Iterate through VSS signals table
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        if (lua_type(L_, -2) == LUA_TNUMBER) {
            VSSSignal signal = extract_vss_signal(-1);
            if (!signal.path.empty()) {
                vss_signals.push_back(signal);
            }
        }
        lua_pop(L_, 1);
    }
    
    lua_pop(L_, 1); // Pop vss_signals table
    
    return vss_signals;
}

bool LuaMapper::execute_mapping_function() {
    lua_getglobal(L_, "map_signals");
    if (!lua_isfunction(L_, -1)) {
        LOG(ERROR) << "map_signals function not found";
        lua_pop(L_, 1);
        return false;
    }
    
    if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
        LOG(ERROR) << "Error executing map_signals: " << lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    
    return true;
}

VSSSignal LuaMapper::extract_vss_signal(int index) {
    VSSSignal signal;

    if (!lua_istable(L_, index)) {
        return signal;
    }

    // Get path
    lua_getfield(L_, index, "path");
    if (lua_isstring(L_, -1)) {
        signal.path = lua_tostring(L_, -1);
    }
    lua_pop(L_, 1);

    // Get value type (for determining how to parse the value)
    std::string value_type_str;
    lua_getfield(L_, index, "type");
    if (lua_isstring(L_, -1)) {
        value_type_str = lua_tostring(L_, -1);
    } else {
        value_type_str = "double"; // Default type
    }
    lua_pop(L_, 1);

    // Get value and convert to appropriate VSS Value type
    lua_getfield(L_, index, "value");
    if (lua_type(L_, -1) == LUA_TNUMBER) {
        signal.qualified_value.value = lua_tonumber(L_, -1);
    } else if (lua_type(L_, -1) == LUA_TBOOLEAN) {
        signal.qualified_value.value = static_cast<bool>(lua_toboolean(L_, -1));
    } else if (lua_type(L_, -1) == LUA_TSTRING) {
        signal.qualified_value.value = std::string(lua_tostring(L_, -1));
    } else if (lua_type(L_, -1) == LUA_TTABLE) {
        // Handle struct values (Lua tables)
        // Convert to lowercase for case-insensitive comparison
        std::string value_type_lower = value_type_str;
        std::transform(value_type_lower.begin(), value_type_lower.end(), value_type_lower.begin(), ::tolower);

        if (value_type_lower == "struct" || value_type_str.find("Types.") == 0) {
            // Convert Lua table to VSS struct with proper type handling
            auto vss_type = value_type_from_string(value_type_str);
            if (!vss_type.has_value() && value_type_lower == "struct") {
                vss_type = ValueType::STRUCT;
            }
            signal.qualified_value.value = VSSTypeHelper::from_lua_table_typed(L_, -1, vss_type.value_or(ValueType::STRUCT));
        } else {
            // Unknown table type, use empty monostate
            signal.qualified_value.value = std::monostate{};
        }
    } else if (lua_type(L_, -1) == LUA_TNIL) {
        // For nil values, use empty monostate
        signal.qualified_value.value = std::monostate{};
    }
    lua_pop(L_, 1);

    // Get quality (formerly status)
    lua_getfield(L_, index, "status");
    if (lua_isinteger(L_, -1)) {
        int status_val = lua_tointeger(L_, -1);
        signal.qualified_value.quality = static_cast<SignalQuality>(status_val);
    } else if (lua_isnumber(L_, -1)) {
        // Handle as number if not specifically integer
        int status_val = static_cast<int>(lua_tonumber(L_, -1));
        signal.qualified_value.quality = static_cast<SignalQuality>(status_val);
    } else {
        // Default to valid if no status field or wrong type
        signal.qualified_value.quality = SignalQuality::VALID;
    }
    lua_pop(L_, 1);

    // Set timestamp to current time
    signal.qualified_value.timestamp = std::chrono::system_clock::now();

    return signal;
}

bool LuaMapper::execute_lua_string(const std::string& lua_code) {
    if (!L_) {
        LOG(ERROR) << "Lua state not initialized";
        return false;
    }
    
    if (luaL_dostring(L_, lua_code.c_str()) != LUA_OK) {
        LOG(ERROR) << "Failed to execute Lua code: " << lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    
    return true;
}

std::optional<VSSSignal> LuaMapper::call_transform_function(const std::string& signal_name, double value) {
    if (!L_) {
        LOG(ERROR) << "Lua state not initialized";
        return std::nullopt;
    }
    
    // Call: process_signal(signal_name, value)
    lua_getglobal(L_, "process_signal");
    if (!lua_isfunction(L_, -1)) {
        LOG(ERROR) << "process_signal function not found";
        lua_pop(L_, 1);
        return std::nullopt;
    }
    
    lua_pushstring(L_, signal_name.c_str());
    lua_pushnumber(L_, value);
    
    if (lua_pcall(L_, 2, 1, 0) != LUA_OK) {
        LOG(ERROR) << "Error calling process_signal: " << lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return std::nullopt;
    }
    
    // Check if result is nil
    if (lua_isnil(L_, -1)) {
        lua_pop(L_, 1);
        return std::nullopt;
    }
    
    auto result = extract_vss_signal_from_stack();
    lua_pop(L_, 1);
    
    return result;
}

std::optional<VSSSignal> LuaMapper::extract_vss_signal_from_stack() {
    if (!lua_istable(L_, -1)) {
        return std::nullopt;
    }

    VSSSignal signal;

    // Get path
    lua_getfield(L_, -1, "path");
    if (lua_isstring(L_, -1)) {
        signal.path = lua_tostring(L_, -1);
    }
    lua_pop(L_, 1);

    if (signal.path.empty()) {
        return std::nullopt;
    }

    // Get value type (for determining how to parse the value)
    std::string value_type_str;
    lua_getfield(L_, -1, "type");
    if (lua_isstring(L_, -1)) {
        value_type_str = lua_tostring(L_, -1);
    } else {
        value_type_str = "double";
    }
    lua_pop(L_, 1);

    // Get value and convert to appropriate VSS Value type
    lua_getfield(L_, -1, "value");
    if (lua_type(L_, -1) == LUA_TNUMBER) {
        signal.qualified_value.value = lua_tonumber(L_, -1);
    } else if (lua_type(L_, -1) == LUA_TBOOLEAN) {
        signal.qualified_value.value = static_cast<bool>(lua_toboolean(L_, -1));
    } else if (lua_type(L_, -1) == LUA_TSTRING) {
        signal.qualified_value.value = std::string(lua_tostring(L_, -1));
    } else if (lua_type(L_, -1) == LUA_TTABLE) {
        // Handle struct values (Lua tables)
        // Convert to lowercase for case-insensitive comparison
        std::string value_type_lower = value_type_str;
        std::transform(value_type_lower.begin(), value_type_lower.end(), value_type_lower.begin(), ::tolower);

        if (value_type_lower == "struct" || value_type_str.find("Types.") == 0) {
            // Convert Lua table to VSS struct with proper type handling
            auto vss_type = value_type_from_string(value_type_str);
            if (!vss_type.has_value() && value_type_lower == "struct") {
                vss_type = ValueType::STRUCT;
            }
            signal.qualified_value.value = VSSTypeHelper::from_lua_table_typed(L_, -1, vss_type.value_or(ValueType::STRUCT));
        } else {
            // Unknown table type, use empty monostate
            signal.qualified_value.value = std::monostate{};
        }
    } else if (lua_type(L_, -1) == LUA_TNIL) {
        // For nil values, use empty monostate
        signal.qualified_value.value = std::monostate{};
    }
    lua_pop(L_, 1);

    // Get quality (formerly status)
    lua_getfield(L_, -1, "status");
    if (lua_isinteger(L_, -1)) {
        int status_val = lua_tointeger(L_, -1);
        signal.qualified_value.quality = static_cast<SignalQuality>(status_val);
    } else if (lua_isnumber(L_, -1)) {
        // Handle as number if not specifically integer
        int status_val = static_cast<int>(lua_tonumber(L_, -1));
        signal.qualified_value.quality = static_cast<SignalQuality>(status_val);
    } else {
        // Default to valid if no status field or wrong type
        signal.qualified_value.quality = SignalQuality::VALID;
    }
    lua_pop(L_, 1);

    // Set timestamp to current time
    signal.qualified_value.timestamp = std::chrono::system_clock::now();

    return signal;
}

std::optional<std::string> LuaMapper::get_lua_variable(const std::string& var_name) {
    if (!L_) return std::nullopt;
    
    // Parse the variable name (handle dots for nested access)
    std::string cmd = "return " + var_name;
    
    if (luaL_dostring(L_, cmd.c_str()) != LUA_OK) {
        lua_pop(L_, 1);  // Pop error message
        return std::nullopt;
    }
    
    if (lua_isnil(L_, -1)) {
        lua_pop(L_, 1);
        return std::nullopt;
    }
    
    std::string result;
    if (lua_isstring(L_, -1) || lua_isnumber(L_, -1)) {
        result = lua_tostring(L_, -1);
    } else if (lua_isboolean(L_, -1)) {
        result = lua_toboolean(L_, -1) ? "true" : "false";
    } else {
        result = lua_typename(L_, lua_type(L_, -1));
    }
    
    lua_pop(L_, 1);
    return result;
}

} // namespace vssdag
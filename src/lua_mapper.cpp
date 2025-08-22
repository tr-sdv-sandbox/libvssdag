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
    
    // Get value type
    lua_getfield(L_, index, "type");
    if (lua_isstring(L_, -1)) {
        signal.value_type = lua_tostring(L_, -1);
    } else {
        signal.value_type = "double"; // Default type
    }
    lua_pop(L_, 1);
    
    // Get value as string
    lua_getfield(L_, index, "value");
    if (lua_type(L_, -1) == LUA_TNUMBER) {
        std::ostringstream oss;
        oss << lua_tonumber(L_, -1);
        signal.value = oss.str();
    } else if (lua_type(L_, -1) == LUA_TBOOLEAN) {
        signal.value = lua_toboolean(L_, -1) ? "true" : "false";
    } else if (lua_type(L_, -1) == LUA_TSTRING) {
        signal.value = lua_tostring(L_, -1);
    } else if (lua_type(L_, -1) == LUA_TTABLE) {
        // Handle struct values (Lua tables)
        if (signal.value_type == "struct" || signal.value_type.find("Types.") == 0) {
            // Convert Lua table to VSS struct with proper type handling
            VSSDataType vss_type = vss_datatype_from_string(signal.value_type);
            if (vss_type == VSSDataType::Unknown && signal.value_type == "struct") {
                vss_type = VSSDataType::Struct;
            }
            VSSValue vss_value = VSSTypeHelper::from_lua_table_typed(L_, -1, vss_type);
            signal.value = VSSTypeHelper::to_json(vss_value);
        } else {
            // Unknown table type, use JSON representation
            signal.value = "{}";  // Placeholder for now
        }
    } else if (lua_type(L_, -1) == LUA_TNIL) {
        // For nil values, indicate no data available
        signal.value = "N/A";
    }
    lua_pop(L_, 1);
    
    // Get status
    lua_getfield(L_, index, "status");
    if (lua_isinteger(L_, -1)) {
        int status_val = lua_tointeger(L_, -1);
        signal.status = static_cast<SignalStatus>(status_val);
    } else if (lua_isnumber(L_, -1)) {
        // Handle as number if not specifically integer
        int status_val = static_cast<int>(lua_tonumber(L_, -1));
        signal.status = static_cast<SignalStatus>(status_val);
    } else {
        // Default to valid if no status field or wrong type
        signal.status = SignalStatus::Valid;
    }
    lua_pop(L_, 1);
    
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
    
    // Get value type
    lua_getfield(L_, -1, "type");
    if (lua_isstring(L_, -1)) {
        signal.value_type = lua_tostring(L_, -1);
    } else {
        signal.value_type = "double";
    }
    lua_pop(L_, 1);
    
    // Get value as string
    lua_getfield(L_, -1, "value");
    if (lua_type(L_, -1) == LUA_TNUMBER) {
        std::ostringstream oss;
        oss << lua_tonumber(L_, -1);
        signal.value = oss.str();
    } else if (lua_type(L_, -1) == LUA_TBOOLEAN) {
        signal.value = lua_toboolean(L_, -1) ? "true" : "false";
    } else if (lua_type(L_, -1) == LUA_TSTRING) {
        signal.value = lua_tostring(L_, -1);
    } else if (lua_type(L_, -1) == LUA_TTABLE) {
        // Handle struct values (Lua tables)
        if (signal.value_type == "struct" || signal.value_type.find("Types.") == 0) {
            // Convert Lua table to VSS struct with proper type handling
            VSSDataType vss_type = vss_datatype_from_string(signal.value_type);
            if (vss_type == VSSDataType::Unknown && signal.value_type == "struct") {
                vss_type = VSSDataType::Struct;
            }
            VSSValue vss_value = VSSTypeHelper::from_lua_table_typed(L_, -1, vss_type);
            signal.value = VSSTypeHelper::to_json(vss_value);
        } else {
            // Unknown table type, use JSON representation
            signal.value = "{}";  // Placeholder for now
        }
    } else if (lua_type(L_, -1) == LUA_TNIL) {
        // For nil values, indicate no data available
        signal.value = "N/A";
    }
    lua_pop(L_, 1);
    
    // Get status
    lua_getfield(L_, -1, "status");
    if (lua_isinteger(L_, -1)) {
        int status_val = lua_tointeger(L_, -1);
        signal.status = static_cast<SignalStatus>(status_val);
    } else if (lua_isnumber(L_, -1)) {
        // Handle as number if not specifically integer
        int status_val = static_cast<int>(lua_tonumber(L_, -1));
        signal.status = static_cast<SignalStatus>(status_val);
    } else {
        // Default to valid if no status field or wrong type
        signal.status = SignalStatus::Valid;
    }
    lua_pop(L_, 1);
    
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
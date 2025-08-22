#include "vssdag/vss_types.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <lua.hpp>

namespace vssdag {

// Convert typed value to appropriate VSS type based on VSS data type enum
VSSValue VSSTypeHelper::from_typed_value(const std::variant<int64_t, double, std::string>& value, VSSDataType target_type) {
    return std::visit([target_type](auto&& val) -> VSSValue {
        using T = std::decay_t<decltype(val)>;
        
        if constexpr (std::is_same_v<T, int64_t>) {
            // Source is integer
            switch (target_type) {
                case VSSDataType::Int8:
                    return VSSInt8(static_cast<VSSInt8>(val));
                case VSSDataType::Int16:
                    return VSSInt16(static_cast<VSSInt16>(val));
                case VSSDataType::Int32:
                    return VSSInt32(static_cast<VSSInt32>(val));
                case VSSDataType::Int64:
                    return VSSInt64(val);
                case VSSDataType::UInt8:
                    return VSSUInt8(static_cast<VSSUInt8>(val));
                case VSSDataType::UInt16:
                    return VSSUInt16(static_cast<VSSUInt16>(val));
                case VSSDataType::UInt32:
                    return VSSUInt32(static_cast<VSSUInt32>(val));
                case VSSDataType::UInt64:
                    return VSSUInt64(static_cast<VSSUInt64>(val));
                case VSSDataType::Float:
                    return VSSFloat(static_cast<VSSFloat>(val));
                case VSSDataType::Double:
                    return VSSDouble(static_cast<VSSDouble>(val));
                case VSSDataType::Boolean:
                    return VSSBoolean(val != 0);
                case VSSDataType::String:
                    return VSSString(std::to_string(val));
                default:
                    return VSSInt64(val);  // Default to int64 for integers
            }
        } else if constexpr (std::is_same_v<T, double>) {
            // Source is double
            switch (target_type) {
                case VSSDataType::Int8:
                    return VSSInt8(static_cast<VSSInt8>(val));
                case VSSDataType::Int16:
                    return VSSInt16(static_cast<VSSInt16>(val));
                case VSSDataType::Int32:
                    return VSSInt32(static_cast<VSSInt32>(val));
                case VSSDataType::Int64:
                    return VSSInt64(static_cast<VSSInt64>(val));
                case VSSDataType::UInt8:
                    return VSSUInt8(static_cast<VSSUInt8>(val));
                case VSSDataType::UInt16:
                    return VSSUInt16(static_cast<VSSUInt16>(val));
                case VSSDataType::UInt32:
                    return VSSUInt32(static_cast<VSSUInt32>(val));
                case VSSDataType::UInt64:
                    return VSSUInt64(static_cast<VSSUInt64>(val));
                case VSSDataType::Float:
                    return VSSFloat(static_cast<VSSFloat>(val));
                case VSSDataType::Double:
                    return VSSDouble(val);
                case VSSDataType::Boolean:
                    return VSSBoolean(val != 0.0);
                case VSSDataType::String:
                    return VSSString(std::to_string(val));
                default:
                    return VSSDouble(val);  // Default to double for floats
            }
        } else {
            // Source is string
            switch (target_type) {
                case VSSDataType::Int8:
                case VSSDataType::Int16:
                case VSSDataType::Int32:
                case VSSDataType::Int64:
                case VSSDataType::UInt8:
                case VSSDataType::UInt16:
                case VSSDataType::UInt32:
                case VSSDataType::UInt64:
                case VSSDataType::Float:
                case VSSDataType::Double:
                    // Try to parse numeric string
                    try {
                        if (val.find('.') != std::string::npos) {
                            // Has decimal point, parse as double
                            double d = std::stod(val);
                            return from_typed_value(d, target_type);
                        } else {
                            // No decimal, parse as int64
                            int64_t i = std::stoll(val);
                            return from_typed_value(i, target_type);
                        }
                    } catch (...) {
                        // Parse failed, return default value
                        return from_typed_value(0.0, target_type);
                    }
                case VSSDataType::Boolean:
                    return VSSBoolean(val == "true" || val == "1");
                case VSSDataType::String:
                    return VSSString(val);
                default:
                    return VSSString(val);
            }
        }
    }, value);
}

// Convert Lua table to VSS struct/array with proper type handling
VSSValue VSSTypeHelper::from_lua_table_typed(void* lua_state, int table_index, VSSDataType datatype) {
    lua_State* L = static_cast<lua_State*>(lua_state);
    
    // Ensure we have the correct absolute index
    if (table_index < 0) {
        table_index = lua_gettop(L) + table_index + 1;
    }
    
    if (datatype == VSSDataType::Struct) {
        // Create a struct
        VSSStruct vss_struct;
        
        // Iterate through table fields
        lua_pushnil(L);  // First key
        while (lua_next(L, table_index) != 0) {
            // Key is at index -2, value at index -1
            
            // Get key as string
            std::string key;
            if (lua_type(L, -2) == LUA_TSTRING) {
                key = lua_tostring(L, -2);
            } else if (lua_type(L, -2) == LUA_TNUMBER) {
                // Skip numeric keys for struct (they should be string keys)
                lua_pop(L, 1);  // Remove value
                continue;
            }
            
            // Convert value based on its Lua type
            VSSValue field_value;
            int value_type = lua_type(L, -1);
            
            switch (value_type) {
                case LUA_TNUMBER: {
                    double num = lua_tonumber(L, -1);
                    // Check if it's an integer in Lua
                    if (lua_isinteger(L, -1)) {
                        field_value = from_typed_value(static_cast<int64_t>(num), VSSDataType::Int64);
                    } else {
                        field_value = from_typed_value(num, VSSDataType::Double);
                    }
                    break;
                }
                    
                case LUA_TBOOLEAN:
                    field_value = VSSBoolean(lua_toboolean(L, -1));
                    break;
                    
                case LUA_TSTRING:
                    field_value = VSSString(lua_tostring(L, -1));
                    break;
                    
                case LUA_TTABLE:
                    // Nested table - assume struct
                    field_value = from_lua_table_typed(L, -1, VSSDataType::Struct);
                    break;
                    
                case LUA_TNIL:
                default:
                    // Use appropriate default value
                    field_value = VSSDouble(0.0);
                    break;
            }
            
            vss_struct.fields[key] = field_value;
            
            lua_pop(L, 1);  // Remove value, keep key for next iteration
        }
        
        return vss_struct;
        
    } else if (datatype == VSSDataType::Array) {
        // Create an array
        VSSArray vss_array;
        
        // Get array length
        size_t len = lua_rawlen(L, table_index);
        
        // Iterate through array elements (1-based in Lua)
        for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(L, table_index, i);
            
            VSSValue element;
            int value_type = lua_type(L, -1);
            
            switch (value_type) {
                case LUA_TNUMBER: {
                    double num = lua_tonumber(L, -1);
                    if (lua_isinteger(L, -1)) {
                        element = from_typed_value(static_cast<int64_t>(num), VSSDataType::Int64);
                    } else {
                        element = from_typed_value(num, VSSDataType::Double);
                    }
                    break;
                }
                    
                case LUA_TBOOLEAN:
                    element = VSSBoolean(lua_toboolean(L, -1));
                    break;
                    
                case LUA_TSTRING:
                    element = VSSString(lua_tostring(L, -1));
                    break;
                    
                case LUA_TTABLE:
                    element = from_lua_table_typed(L, -1, VSSDataType::Struct);
                    break;
                    
                default:
                    element = VSSDouble(0.0);
                    break;
            }
            
            vss_array.elements.push_back(element);
            lua_pop(L, 1);
        }
        
        return vss_array;
        
    } else {
        // Unknown type, treat as struct
        return from_lua_table_typed(lua_state, table_index, VSSDataType::Struct);
    }
}

// Push typed value to Lua stack preserving type information
void VSSTypeHelper::push_typed_value_to_lua(void* lua_state, const std::variant<int64_t, double, std::string>& value) {
    lua_State* L = static_cast<lua_State*>(lua_state);
    
    std::visit([L](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        
        if constexpr (std::is_same_v<T, int64_t>) {
            lua_pushinteger(L, val);
        } else if constexpr (std::is_same_v<T, double>) {
            lua_pushnumber(L, val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            lua_pushstring(L, val.c_str());
        }
    }, value);
}

// Push VSS value to Lua stack preserving type information
void VSSTypeHelper::push_vss_value_to_lua(void* lua_state, const VSSValue& value) {
    lua_State* L = static_cast<lua_State*>(lua_state);
    
    std::visit([L](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        
        if constexpr (std::is_same_v<T, VSSInt8> || std::is_same_v<T, VSSInt16> ||
                      std::is_same_v<T, VSSInt32> || std::is_same_v<T, VSSInt64>) {
            // Push as integer
            lua_pushinteger(L, static_cast<lua_Integer>(val));
        } else if constexpr (std::is_same_v<T, VSSUInt8> || std::is_same_v<T, VSSUInt16> ||
                      std::is_same_v<T, VSSUInt32> || std::is_same_v<T, VSSUInt64>) {
            // Push as integer (Lua doesn't have unsigned)
            lua_pushinteger(L, static_cast<lua_Integer>(val));
        } else if constexpr (std::is_same_v<T, VSSFloat> || std::is_same_v<T, VSSDouble>) {
            // Push as number
            lua_pushnumber(L, static_cast<lua_Number>(val));
        } else if constexpr (std::is_same_v<T, VSSBoolean>) {
            // Push as boolean
            lua_pushboolean(L, val);
        } else if constexpr (std::is_same_v<T, VSSString>) {
            // Push as string
            lua_pushstring(L, val.c_str());
        } else if constexpr (std::is_same_v<T, VSSStruct>) {
            // Push as table
            lua_newtable(L);
            for (const auto& [key, field_value] : val.fields) {
                lua_pushstring(L, key.c_str());
                push_vss_value_to_lua(L, field_value);
                lua_settable(L, -3);
            }
        } else if constexpr (std::is_same_v<T, VSSArray>) {
            // Push as array (1-indexed in Lua)
            lua_newtable(L);
            for (size_t i = 0; i < val.elements.size(); ++i) {
                lua_pushinteger(L, i + 1);  // Lua arrays are 1-indexed
                push_vss_value_to_lua(L, val.elements[i]);
                lua_settable(L, -3);
            }
        }
    }, value);
}

// Format VSS value as string for output
std::string VSSTypeHelper::to_string(const VSSValue& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        
        if constexpr (std::is_same_v<T, VSSInt8> || std::is_same_v<T, VSSInt16> ||
                      std::is_same_v<T, VSSInt32> || std::is_same_v<T, VSSInt64> ||
                      std::is_same_v<T, VSSUInt8> || std::is_same_v<T, VSSUInt16> ||
                      std::is_same_v<T, VSSUInt32> || std::is_same_v<T, VSSUInt64>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, VSSFloat> || std::is_same_v<T, VSSDouble>) {
            // Clean up floating point display
            if (std::abs(v) < 1e-6) {
                return "0";
            }
            std::stringstream ss;
            ss << std::fixed << std::setprecision(6) << v;
            std::string result = ss.str();
            // Remove trailing zeros
            result.erase(result.find_last_not_of('0') + 1, std::string::npos);
            if (result.back() == '.') {
                result.pop_back();
            }
            return result;
        } else if constexpr (std::is_same_v<T, VSSBoolean>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, VSSString>) {
            return v;
        } else if constexpr (std::is_same_v<T, VSSStruct>) {
            return to_json(v);  // Use JSON format for structs
        } else if constexpr (std::is_same_v<T, VSSArray>) {
            return to_json(v);  // Use JSON format for arrays
        }
        return "";
    }, value);
}

// Format VSS value as JSON string
std::string VSSTypeHelper::to_json(const VSSValue& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        
        if constexpr (std::is_same_v<T, VSSInt8> || std::is_same_v<T, VSSInt16> ||
                      std::is_same_v<T, VSSInt32> || std::is_same_v<T, VSSInt64> ||
                      std::is_same_v<T, VSSUInt8> || std::is_same_v<T, VSSUInt16> ||
                      std::is_same_v<T, VSSUInt32> || std::is_same_v<T, VSSUInt64>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, VSSFloat> || std::is_same_v<T, VSSDouble>) {
            // Clean up floating point display
            if (std::abs(v) < 1e-6) {
                return "0";
            }
            std::stringstream ss;
            ss << std::fixed << std::setprecision(6) << v;
            std::string result = ss.str();
            // Remove trailing zeros
            result.erase(result.find_last_not_of('0') + 1, std::string::npos);
            if (result.back() == '.') {
                result.pop_back();
            }
            return result;
        } else if constexpr (std::is_same_v<T, VSSBoolean>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, VSSString>) {
            // Escape string for JSON
            std::stringstream ss;
            ss << "\"";
            for (char c : v) {
                switch (c) {
                    case '"': ss << "\\\""; break;
                    case '\\': ss << "\\\\"; break;
                    case '\b': ss << "\\b"; break;
                    case '\f': ss << "\\f"; break;
                    case '\n': ss << "\\n"; break;
                    case '\r': ss << "\\r"; break;
                    case '\t': ss << "\\t"; break;
                    default:
                        if (c >= 0x20 && c <= 0x7E) {
                            ss << c;
                        } else {
                            ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                        }
                }
            }
            ss << "\"";
            return ss.str();
        } else if constexpr (std::is_same_v<T, VSSStruct>) {
            std::stringstream ss;
            ss << "{";
            bool first = true;
            for (const auto& [key, field_value] : v.fields) {
                if (!first) ss << ",";
                ss << "\"" << key << "\":" << to_json(field_value);
                first = false;
            }
            ss << "}";
            return ss.str();
        } else if constexpr (std::is_same_v<T, VSSArray>) {
            std::stringstream ss;
            ss << "[";
            bool first = true;
            for (const auto& element : v.elements) {
                if (!first) ss << ",";
                ss << to_json(element);
                first = false;
            }
            ss << "]";
            return ss.str();
        }
        return "null";
    }, value);
}

} // namespace vssdag
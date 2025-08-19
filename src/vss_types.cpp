#include "libVSSDAG/vss_types.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <lua.hpp>

namespace can_to_vss {

// Convert Lua number to appropriate VSS type based on datatype string
VSSValue VSSTypeHelper::from_lua_value(double lua_number, const std::string& datatype) {
    if (datatype == "int8") {
        return cast_numeric<VSSInt8>(lua_number);
    } else if (datatype == "int16") {
        return cast_numeric<VSSInt16>(lua_number);
    } else if (datatype == "int32") {
        return cast_numeric<VSSInt32>(lua_number);
    } else if (datatype == "int64") {
        return cast_numeric<VSSInt64>(lua_number);
    } else if (datatype == "uint8") {
        return cast_numeric<VSSUInt8>(lua_number);
    } else if (datatype == "uint16") {
        return cast_numeric<VSSUInt16>(lua_number);
    } else if (datatype == "uint32") {
        return cast_numeric<VSSUInt32>(lua_number);
    } else if (datatype == "uint64") {
        return cast_numeric<VSSUInt64>(lua_number);
    } else if (datatype == "float") {
        return cast_numeric<VSSFloat>(lua_number);
    } else if (datatype == "double") {
        return cast_numeric<VSSDouble>(lua_number);
    } else if (datatype == "boolean") {
        return VSSBoolean(lua_number != 0);
    } else {
        // Default to double for unknown numeric types
        return cast_numeric<VSSDouble>(lua_number);
    }
}

// Convert Lua string to appropriate VSS type
VSSValue VSSTypeHelper::from_lua_string(const std::string& lua_string, const std::string& datatype) {
    if (datatype == "string") {
        return VSSString(lua_string);
    } else if (datatype == "boolean") {
        return VSSBoolean(lua_string == "true" || lua_string == "1");
    } else if (is_numeric_type(datatype)) {
        // Try to parse string as number
        try {
            double value = std::stod(lua_string);
            return from_lua_value(value, datatype);
        } catch (...) {
            // If parsing fails, return 0
            return from_lua_value(0.0, datatype);
        }
    } else {
        return VSSString(lua_string);
    }
}

// Convert Lua boolean to VSS boolean
VSSValue VSSTypeHelper::from_lua_boolean(bool lua_bool) {
    return VSSBoolean(lua_bool);
}

// Convert Lua table to appropriate VSS type (struct or array)
VSSValue VSSTypeHelper::from_lua_table(void* lua_state, int table_index, const std::string& datatype) {
    lua_State* L = static_cast<lua_State*>(lua_state);
    
    // Ensure we have the correct absolute index
    if (table_index < 0) {
        table_index = lua_gettop(L) + table_index + 1;
    }
    
    if (is_struct_type(datatype)) {
        // Create a struct
        VSSStruct vss_struct;
        vss_struct.type_name = datatype;
        
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
            
            // Convert value based on its type
            VSSValue field_value;
            int value_type = lua_type(L, -1);
            
            switch (value_type) {
                case LUA_TNUMBER:
                    field_value = VSSDouble(lua_tonumber(L, -1));
                    break;
                    
                case LUA_TBOOLEAN:
                    field_value = VSSBoolean(lua_toboolean(L, -1));
                    break;
                    
                case LUA_TSTRING:
                    field_value = VSSString(lua_tostring(L, -1));
                    break;
                    
                case LUA_TTABLE:
                    // Nested table - could be another struct or array
                    field_value = from_lua_table(L, -1, "struct");
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
        
    } else if (is_array_type(datatype)) {
        // Create an array
        VSSArray vss_array;
        
        // Extract element type from datatype (e.g., "array[float]" -> "float")
        size_t bracket_pos = datatype.find('[');
        if (bracket_pos != std::string::npos) {
            size_t end_bracket = datatype.find(']', bracket_pos);
            if (end_bracket != std::string::npos) {
                vss_array.element_type = datatype.substr(bracket_pos + 1, end_bracket - bracket_pos - 1);
            }
        }
        
        // Get array length
        size_t len = lua_rawlen(L, table_index);
        
        // Iterate through array elements (1-based in Lua)
        for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(L, table_index, i);
            
            VSSValue element;
            int value_type = lua_type(L, -1);
            
            switch (value_type) {
                case LUA_TNUMBER:
                    if (!vss_array.element_type.empty()) {
                        element = from_lua_value(lua_tonumber(L, -1), vss_array.element_type);
                    } else {
                        element = VSSDouble(lua_tonumber(L, -1));
                    }
                    break;
                    
                case LUA_TBOOLEAN:
                    element = VSSBoolean(lua_toboolean(L, -1));
                    break;
                    
                case LUA_TSTRING:
                    element = VSSString(lua_tostring(L, -1));
                    break;
                    
                case LUA_TTABLE:
                    element = from_lua_table(L, -1, vss_array.element_type);
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
        // Unknown table type, treat as struct
        return from_lua_table(lua_state, table_index, "struct");
    }
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

// Type checking utilities
bool VSSTypeHelper::is_numeric_type(const std::string& datatype) {
    return datatype == "int8" || datatype == "int16" || datatype == "int32" || datatype == "int64" ||
           datatype == "uint8" || datatype == "uint16" || datatype == "uint32" || datatype == "uint64" ||
           datatype == "float" || datatype == "double";
}

bool VSSTypeHelper::is_integer_type(const std::string& datatype) {
    return datatype == "int8" || datatype == "int16" || datatype == "int32" || datatype == "int64" ||
           datatype == "uint8" || datatype == "uint16" || datatype == "uint32" || datatype == "uint64";
}

bool VSSTypeHelper::is_unsigned_type(const std::string& datatype) {
    return datatype == "uint8" || datatype == "uint16" || datatype == "uint32" || datatype == "uint64";
}

bool VSSTypeHelper::is_struct_type(const std::string& datatype) {
    return datatype == "struct" || datatype.find("Types.") == 0;
}

bool VSSTypeHelper::is_array_type(const std::string& datatype) {
    return datatype.find("array") == 0 || datatype.find('[') != std::string::npos;
}

} // namespace can_to_vss
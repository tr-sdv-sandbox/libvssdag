#include "vssdag/vss_types.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <lua.hpp>

namespace vssdag {

// Convert typed value to appropriate VSS type based on VSS data type enum
Value VSSTypeHelper::from_typed_value(const Value& value, ValueType target_type) {
    return std::visit([target_type, &value](auto&& val) -> Value {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>) {
            // Source is integer
            switch (target_type) {
                case ValueType::INT32:
                    return static_cast<int32_t>(val);
                case ValueType::INT64:
                    return static_cast<int64_t>(val);
                case ValueType::UINT32:
                    return static_cast<uint32_t>(val);
                case ValueType::UINT64:
                    return static_cast<uint64_t>(val);
                case ValueType::FLOAT:
                    return static_cast<float>(val);
                case ValueType::DOUBLE:
                    return static_cast<double>(val);
                case ValueType::BOOL:
                    return val != 0;
                case ValueType::STRING:
                    return std::to_string(val);
                default:
                    return static_cast<int64_t>(val);  // Default to int64 for integers
            }
        } else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
            // Source is unsigned integer
            switch (target_type) {
                case ValueType::INT32:
                    return static_cast<int32_t>(val);
                case ValueType::INT64:
                    return static_cast<int64_t>(val);
                case ValueType::UINT32:
                    return static_cast<uint32_t>(val);
                case ValueType::UINT64:
                    return static_cast<uint64_t>(val);
                case ValueType::FLOAT:
                    return static_cast<float>(val);
                case ValueType::DOUBLE:
                    return static_cast<double>(val);
                case ValueType::BOOL:
                    return val != 0;
                case ValueType::STRING:
                    return std::to_string(val);
                default:
                    return static_cast<uint64_t>(val);  // Default to uint64 for unsigned
            }
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            // Source is floating point
            switch (target_type) {
                case ValueType::INT32:
                    return static_cast<int32_t>(val);
                case ValueType::INT64:
                    return static_cast<int64_t>(val);
                case ValueType::UINT32:
                    return static_cast<uint32_t>(val);
                case ValueType::UINT64:
                    return static_cast<uint64_t>(val);
                case ValueType::FLOAT:
                    return static_cast<float>(val);
                case ValueType::DOUBLE:
                    return static_cast<double>(val);
                case ValueType::BOOL:
                    return val != 0.0;
                case ValueType::STRING:
                    return std::to_string(val);
                default:
                    return static_cast<double>(val);  // Default to double for floats
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Source is string
            switch (target_type) {
                case ValueType::INT32:
                case ValueType::INT64:
                case ValueType::UINT32:
                case ValueType::UINT64:
                case ValueType::FLOAT:
                case ValueType::DOUBLE:
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
                case ValueType::BOOL:
                    return val == "true" || val == "1";
                case ValueType::STRING:
                    return val;
                default:
                    return val;
            }
        } else {
            // For other types (bool, monostate, etc.), try to preserve
            return value;
        }
    }, value);
}

// Convert Lua table to VSS struct/array with proper type handling
Value VSSTypeHelper::from_lua_table_typed(void* lua_state, int table_index, ValueType datatype) {
    lua_State* L = static_cast<lua_State*>(lua_state);

    // Ensure we have the correct absolute index
    if (table_index < 0) {
        table_index = lua_gettop(L) + table_index + 1;
    }

    if (datatype == ValueType::STRUCT) {
        // Create a struct (type name unknown, use generic)
        auto vss_struct = std::make_shared<StructValue>("DynamicStruct");

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
            Value field_value;
            int value_type = lua_type(L, -1);

            switch (value_type) {
                case LUA_TNUMBER: {
                    double num = lua_tonumber(L, -1);
                    // Check if it's an integer in Lua
                    if (lua_isinteger(L, -1)) {
                        field_value = from_typed_value(static_cast<int64_t>(num), ValueType::INT64);
                    } else {
                        field_value = from_typed_value(num, ValueType::DOUBLE);
                    }
                    break;
                }

                case LUA_TBOOLEAN:
                    field_value = static_cast<bool>(lua_toboolean(L, -1));
                    break;

                case LUA_TSTRING:
                    field_value = std::string(lua_tostring(L, -1));
                    break;

                case LUA_TTABLE:
                    // Nested table - assume struct
                    field_value = from_lua_table_typed(L, -1, ValueType::STRUCT);
                    break;

                case LUA_TNIL:
                default:
                    // Use appropriate default value
                    field_value = 0.0;
                    break;
            }

            vss_struct->set_field(key, field_value);

            lua_pop(L, 1);  // Remove value, keep key for next iteration
        }

        return vss_struct;

    } else if (is_array(datatype)) {
        // For arrays, we need to determine element type
        // Get array length
        size_t len = lua_rawlen(L, table_index);

        if (len == 0) {
            // Empty array - return empty double array by default
            return std::vector<double>();
        }

        // Peek at first element to determine type
        lua_rawgeti(L, table_index, 1);
        int first_type = lua_type(L, -1);
        lua_pop(L, 1);

        // Build appropriate array type based on first element
        if (first_type == LUA_TNUMBER) {
            // Check if integers or doubles
            lua_rawgeti(L, table_index, 1);
            bool is_int = lua_isinteger(L, -1);
            lua_pop(L, 1);

            if (is_int) {
                std::vector<int64_t> arr;
                for (size_t i = 1; i <= len; ++i) {
                    lua_rawgeti(L, table_index, i);
                    arr.push_back(static_cast<int64_t>(lua_tonumber(L, -1)));
                    lua_pop(L, 1);
                }
                return arr;
            } else {
                std::vector<double> arr;
                for (size_t i = 1; i <= len; ++i) {
                    lua_rawgeti(L, table_index, i);
                    arr.push_back(lua_tonumber(L, -1));
                    lua_pop(L, 1);
                }
                return arr;
            }
        } else if (first_type == LUA_TBOOLEAN) {
            std::vector<bool> arr;
            for (size_t i = 1; i <= len; ++i) {
                lua_rawgeti(L, table_index, i);
                arr.push_back(lua_toboolean(L, -1));
                lua_pop(L, 1);
            }
            return arr;
        } else if (first_type == LUA_TSTRING) {
            std::vector<std::string> arr;
            for (size_t i = 1; i <= len; ++i) {
                lua_rawgeti(L, table_index, i);
                arr.push_back(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            return arr;
        } else if (first_type == LUA_TTABLE) {
            // Array of structs
            std::vector<std::shared_ptr<StructValue>> arr;
            for (size_t i = 1; i <= len; ++i) {
                lua_rawgeti(L, table_index, i);
                Value elem = from_lua_table_typed(L, -1, ValueType::STRUCT);
                if (std::holds_alternative<std::shared_ptr<StructValue>>(elem)) {
                    arr.push_back(std::get<std::shared_ptr<StructValue>>(elem));
                }
                lua_pop(L, 1);
            }
            return arr;
        }

        // Default to empty double array
        return std::vector<double>();

    } else {
        // Unknown type, treat as struct
        return from_lua_table_typed(lua_state, table_index, ValueType::STRUCT);
    }
}

// Push VSS value to Lua stack preserving type information
void VSSTypeHelper::push_value_to_lua(void* lua_state, const Value& value) {
    lua_State* L = static_cast<lua_State*>(lua_state);

    std::visit([L](auto&& val) {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            // Push nil for empty value
            lua_pushnil(L);
        } else if constexpr (std::is_same_v<T, bool>) {
            lua_pushboolean(L, val);
        } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                             std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
            // Push as integer
            lua_pushinteger(L, static_cast<lua_Integer>(val));
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            // Push as number
            lua_pushnumber(L, static_cast<lua_Number>(val));
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Push as string
            lua_pushstring(L, val.c_str());
        } else if constexpr (std::is_same_v<T, std::shared_ptr<StructValue>>) {
            // Push struct as table
            lua_newtable(L);
            for (const auto& [key, field_value] : val->fields()) {
                lua_pushstring(L, key.c_str());
                push_value_to_lua(L, field_value);
                lua_settable(L, -3);
            }
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            // Push bool array as table (1-indexed in Lua)
            lua_newtable(L);
            for (size_t i = 0; i < val.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushboolean(L, val[i]);
                lua_settable(L, -3);
            }
        } else if constexpr (std::is_same_v<T, std::vector<int32_t>> ||
                             std::is_same_v<T, std::vector<int64_t>> ||
                             std::is_same_v<T, std::vector<uint32_t>> ||
                             std::is_same_v<T, std::vector<uint64_t>>) {
            // Push integer array as table (1-indexed in Lua)
            lua_newtable(L);
            for (size_t i = 0; i < val.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushinteger(L, static_cast<lua_Integer>(val[i]));
                lua_settable(L, -3);
            }
        } else if constexpr (std::is_same_v<T, std::vector<float>> ||
                             std::is_same_v<T, std::vector<double>>) {
            // Push float array as table (1-indexed in Lua)
            lua_newtable(L);
            for (size_t i = 0; i < val.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushnumber(L, static_cast<lua_Number>(val[i]));
                lua_settable(L, -3);
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            // Push string array as table (1-indexed in Lua)
            lua_newtable(L);
            for (size_t i = 0; i < val.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushstring(L, val[i].c_str());
                lua_settable(L, -3);
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::shared_ptr<StructValue>>>) {
            // Push struct array as table (1-indexed in Lua)
            lua_newtable(L);
            for (size_t i = 0; i < val.size(); ++i) {
                lua_pushinteger(L, i + 1);
                push_value_to_lua(L, val[i]);
                lua_settable(L, -3);
            }
        }
    }, value);
}

// Format VSS value as string for output
std::string VSSTypeHelper::to_string(const Value& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                             std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
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
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<StructValue>>) {
            return to_json(v);  // Use JSON format for structs
        } else if constexpr (std::is_same_v<T, std::vector<bool>> ||
                             std::is_same_v<T, std::vector<int32_t>> ||
                             std::is_same_v<T, std::vector<int64_t>> ||
                             std::is_same_v<T, std::vector<uint32_t>> ||
                             std::is_same_v<T, std::vector<uint64_t>> ||
                             std::is_same_v<T, std::vector<float>> ||
                             std::is_same_v<T, std::vector<double>> ||
                             std::is_same_v<T, std::vector<std::string>> ||
                             std::is_same_v<T, std::vector<std::shared_ptr<StructValue>>>) {
            return to_json(v);  // Use JSON format for arrays
        }
        return "";
    }, value);
}

// Format VSS value as JSON string
std::string VSSTypeHelper::to_json(const Value& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                             std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
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
        } else if constexpr (std::is_same_v<T, std::string>) {
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
        } else if constexpr (std::is_same_v<T, std::shared_ptr<StructValue>>) {
            std::stringstream ss;
            ss << "{";
            bool first = true;
            for (const auto& [key, field_value] : v->fields()) {
                if (!first) ss << ",";
                ss << "\"" << key << "\":" << to_json(field_value);
                first = false;
            }
            ss << "}";
            return ss.str();
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) ss << ",";
                ss << (v[i] ? "true" : "false");
            }
            ss << "]";
            return ss.str();
        } else if constexpr (std::is_same_v<T, std::vector<int32_t>> ||
                             std::is_same_v<T, std::vector<int64_t>> ||
                             std::is_same_v<T, std::vector<uint32_t>> ||
                             std::is_same_v<T, std::vector<uint64_t>>) {
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) ss << ",";
                ss << v[i];
            }
            ss << "]";
            return ss.str();
        } else if constexpr (std::is_same_v<T, std::vector<float>> ||
                             std::is_same_v<T, std::vector<double>>) {
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) ss << ",";
                if (std::abs(v[i]) < 1e-6) {
                    ss << "0";
                } else {
                    ss << std::fixed << std::setprecision(6) << v[i];
                }
            }
            ss << "]";
            return ss.str();
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) ss << ",";
                ss << to_json(v[i]);
            }
            ss << "]";
            return ss.str();
        } else if constexpr (std::is_same_v<T, std::vector<std::shared_ptr<StructValue>>>) {
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) ss << ",";
                ss << to_json(v[i]);
            }
            ss << "]";
            return ss.str();
        }
        return "null";
    }, value);
}

} // namespace vssdag
#pragma once

#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace can_to_vss {

// VSS primitive types
using VSSInt8 = int8_t;
using VSSInt16 = int16_t;
using VSSInt32 = int32_t;
using VSSInt64 = int64_t;
using VSSUInt8 = uint8_t;
using VSSUInt16 = uint16_t;
using VSSUInt32 = uint32_t;
using VSSUInt64 = uint64_t;
using VSSFloat = float;
using VSSDouble = double;
using VSSBoolean = bool;
using VSSString = std::string;

// Forward declarations for complex types
struct VSSStruct;
struct VSSArray;

// Comprehensive VSS value type that can hold any VSS data
using VSSValue = std::variant<
    VSSInt8, VSSInt16, VSSInt32, VSSInt64,
    VSSUInt8, VSSUInt16, VSSUInt32, VSSUInt64,
    VSSFloat, VSSDouble,
    VSSBoolean,
    VSSString,
    VSSStruct,
    VSSArray
>;

// Struct type - holds named fields
struct VSSStruct {
    std::unordered_map<std::string, VSSValue> fields;
    std::string type_name;  // e.g., "Types.Location"
};

// Array type - holds multiple values of the same type
struct VSSArray {
    std::vector<VSSValue> elements;
    std::string element_type;  // e.g., "float", "struct"
};

// Helper class for type conversion and formatting
class VSSTypeHelper {
public:
    // Convert Lua value to appropriate VSS type based on datatype string
    static VSSValue from_lua_value(double lua_number, const std::string& datatype);
    static VSSValue from_lua_string(const std::string& lua_string, const std::string& datatype);
    static VSSValue from_lua_boolean(bool lua_bool);
    static VSSValue from_lua_table(void* lua_state, int table_index, const std::string& datatype);
    
    // Format VSS value as string for output
    static std::string to_string(const VSSValue& value);
    static std::string to_json(const VSSValue& value);
    
    // Type checking utilities
    static bool is_numeric_type(const std::string& datatype);
    static bool is_integer_type(const std::string& datatype);
    static bool is_unsigned_type(const std::string& datatype);
    static bool is_struct_type(const std::string& datatype);
    static bool is_array_type(const std::string& datatype);
    
    // Convert string datatype to appropriate cast
    template<typename T>
    static T cast_numeric(double value);
};

// Template specializations for numeric casting
template<> inline VSSInt8 VSSTypeHelper::cast_numeric<VSSInt8>(double value) {
    return static_cast<VSSInt8>(value);
}

template<> inline VSSInt16 VSSTypeHelper::cast_numeric<VSSInt16>(double value) {
    return static_cast<VSSInt16>(value);
}

template<> inline VSSInt32 VSSTypeHelper::cast_numeric<VSSInt32>(double value) {
    return static_cast<VSSInt32>(value);
}

template<> inline VSSInt64 VSSTypeHelper::cast_numeric<VSSInt64>(double value) {
    return static_cast<VSSInt64>(value);
}

template<> inline VSSUInt8 VSSTypeHelper::cast_numeric<VSSUInt8>(double value) {
    return static_cast<VSSUInt8>(value);
}

template<> inline VSSUInt16 VSSTypeHelper::cast_numeric<VSSUInt16>(double value) {
    return static_cast<VSSUInt16>(value);
}

template<> inline VSSUInt32 VSSTypeHelper::cast_numeric<VSSUInt32>(double value) {
    return static_cast<VSSUInt32>(value);
}

template<> inline VSSUInt64 VSSTypeHelper::cast_numeric<VSSUInt64>(double value) {
    return static_cast<VSSUInt64>(value);
}

template<> inline VSSFloat VSSTypeHelper::cast_numeric<VSSFloat>(double value) {
    return static_cast<VSSFloat>(value);
}

template<> inline VSSDouble VSSTypeHelper::cast_numeric<VSSDouble>(double value) {
    return value;
}

} // namespace can_to_vss
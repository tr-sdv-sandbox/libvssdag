#pragma once

#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace can_to_vss {

// VSS data type enumeration
enum class VSSDataType {
    // Integer types
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    
    // Floating point types
    Float,
    Double,
    
    // Other primitive types
    Boolean,
    String,
    
    // Complex types
    Struct,
    Array,
    
    // Unknown/Invalid
    Unknown
};

// Helper to convert string datatype to enum
inline VSSDataType vss_datatype_from_string(const std::string& datatype) {
    if (datatype == "int8") return VSSDataType::Int8;
    if (datatype == "int16") return VSSDataType::Int16;
    if (datatype == "int32") return VSSDataType::Int32;
    if (datatype == "int64") return VSSDataType::Int64;
    if (datatype == "uint8") return VSSDataType::UInt8;
    if (datatype == "uint16") return VSSDataType::UInt16;
    if (datatype == "uint32") return VSSDataType::UInt32;
    if (datatype == "uint64") return VSSDataType::UInt64;
    if (datatype == "float") return VSSDataType::Float;
    if (datatype == "double") return VSSDataType::Double;
    if (datatype == "boolean") return VSSDataType::Boolean;
    if (datatype == "string") return VSSDataType::String;
    if (datatype == "struct") return VSSDataType::Struct;
    if (datatype == "array") return VSSDataType::Array;
    return VSSDataType::Unknown;
}

// Helper to convert enum to string
inline const char* vss_datatype_to_string(VSSDataType type) {
    switch (type) {
        case VSSDataType::Int8: return "int8";
        case VSSDataType::Int16: return "int16";
        case VSSDataType::Int32: return "int32";
        case VSSDataType::Int64: return "int64";
        case VSSDataType::UInt8: return "uint8";
        case VSSDataType::UInt16: return "uint16";
        case VSSDataType::UInt32: return "uint32";
        case VSSDataType::UInt64: return "uint64";
        case VSSDataType::Float: return "float";
        case VSSDataType::Double: return "double";
        case VSSDataType::Boolean: return "boolean";
        case VSSDataType::String: return "string";
        case VSSDataType::Struct: return "struct";
        case VSSDataType::Array: return "array";
        default: return "unknown";
    }
}

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
    // Convert typed value to appropriate VSS type based on VSS data type enum
    static VSSValue from_typed_value(const std::variant<int64_t, double, std::string>& value, VSSDataType target_type);
    
    // Convert Lua table to VSS struct/array with proper type handling
    static VSSValue from_lua_table_typed(void* lua_state, int table_index, VSSDataType datatype);
    
    // Push typed value to Lua stack preserving type information
    static void push_typed_value_to_lua(void* lua_state, const std::variant<int64_t, double, std::string>& value);
    
    // Push VSS value to Lua stack preserving type information
    static void push_vss_value_to_lua(void* lua_state, const VSSValue& value);
    
    // Format VSS value as string for output
    static std::string to_string(const VSSValue& value);
    static std::string to_json(const VSSValue& value);
};

} // namespace can_to_vss
#pragma once

#include <string>
#include <variant>
#include <cstdint>

namespace can_to_vss {

// DBC signal value types after decoding
// These are the types we get from the DBC library after it handles scaling/offset
enum class DBCValueType {
    // Integer types (signed or unsigned)
    Integer,
    
    // Floating point
    Float,
    
    // String
    String,
    
    // Unknown/unspecified
    Unknown
};

// DBC decoded value - what we get from the DBC library
struct DBCDecodedValue {
    // The decoded value after scaling/offset
    std::variant<int64_t, double, std::string> value;
    
    // Optional: if this integer has enum mappings
    bool has_enums = false;
    
    // Helper to get value as double
    double as_double() const {
        return std::visit([](auto&& val) -> double {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return 0.0; // String to double = 0
            } else {
                return static_cast<double>(val);
            }
        }, value);
    }
    
    // Helper to get value as integer
    int64_t as_int64() const {
        return std::visit([](auto&& val) -> int64_t {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return 0; // String to int = 0
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return val;
            } else {
                return static_cast<int64_t>(val); // Truncate double
            }
        }, value);
    }
    
    // Helper to get value as string
    std::string as_string() const {
        return std::visit([](auto&& val) -> std::string {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return val;
            } else {
                return std::to_string(val);
            }
        }, value);
    }
    
    // Type checkers
    bool is_integer() const { return std::holds_alternative<int64_t>(value); }
    bool is_float() const { return std::holds_alternative<double>(value); }
    bool is_string() const { return std::holds_alternative<std::string>(value); }
    
    // Infer type
    DBCValueType type() const {
        if (is_integer()) return DBCValueType::Integer;
        if (is_float()) return DBCValueType::Float;
        if (is_string()) return DBCValueType::String;
        return DBCValueType::Unknown;
    }
};

// Raw signal update from DBC decoding (before name mapping)
struct DBCSignalUpdate {
    std::string dbc_signal_name;  // Original DBC signal name
    std::variant<int64_t, double, std::string> value;
    bool has_enums = false;
};

} // namespace can_to_vss
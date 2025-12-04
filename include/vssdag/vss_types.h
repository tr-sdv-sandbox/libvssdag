#pragma once

#include <vss/types/types.hpp>

namespace vssdag {

// Re-export vss::types for convenience
using namespace vss::types;

// Helper class for Lua integration and formatting
class VSSTypeHelper {
public:
    // Convert typed value to appropriate VSS type based on VSS data type enum
    static Value from_typed_value(const Value& value, ValueType target_type);

    // Convert Lua table to VSS struct/array with proper type handling
    static Value from_lua_table_typed(void* lua_state, int table_index, ValueType datatype);

    // Push typed value to Lua stack preserving type information
    static void push_value_to_lua(void* lua_state, const Value& value);

    // Format VSS value as string for output
    static std::string to_string(const Value& value);
    static std::string to_json(const Value& value);

    // Value comparison - returns true if values are equal
    static bool values_equal(const Value& a, const Value& b);

    // Check if numeric value changed beyond threshold
    // Returns true if change exceeds threshold, false otherwise
    // For non-numeric types, returns true if values differ
    static bool value_changed_beyond_threshold(const Value& old_val, const Value& new_val, double threshold);

    // Extract numeric value from Value variant (returns 0 if not numeric)
    static double to_double(const Value& value);
};

} // namespace vssdag

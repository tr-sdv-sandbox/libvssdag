#include "libVSSDAG/signal_mapper.h"
#include <glog/logging.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <yaml-cpp/yaml.h>

namespace can_to_vss {

SignalMapper::SignalMapper() {
    lua_mapper_ = std::make_unique<LuaMapper>();
}

SignalMapper::~SignalMapper() = default;

bool SignalMapper::load_mapping_file(const std::string& yaml_file, const DBCParser* dbc_parser) {
    try {
        YAML::Node root = YAML::LoadFile(yaml_file);
        
        if (!root["mappings"]) {
            LOG(ERROR) << "No 'mappings' section found in YAML file";
            return false;
        }
        
        // Clear existing mappings
        signal_mappings_.clear();
        
        // Parse each mapping
        const YAML::Node& mappings = root["mappings"];
        for (const auto& mapping_node : mappings) {
            if (!mapping_node["signal"]) {
                LOG(WARNING) << "Skipping mapping without signal";
                continue;
            }
            
            std::string signal_name = mapping_node["signal"].as<std::string>();
            
            // Check if this is a DBC signal (has dbc_name field)
            std::string dbc_signal_name = signal_name;
            if (mapping_node["dbc_name"]) {
                dbc_signal_name = mapping_node["dbc_name"].as<std::string>();
            }
            
            SignalMapping mapping;
            // vss_path is now optional - intermediate signals may not have it
            if (mapping_node["vss_path"]) {
                mapping.vss_path = mapping_node["vss_path"].as<std::string>();
            }
            mapping.datatype = mapping_node["datatype"].as<std::string>("double");
            mapping.interval_ms = mapping_node["interval_ms"].as<int>(0);
            
            // Check if this is a struct type (VSS 4.0)
            if (mapping.datatype == "struct") {
                mapping.is_struct = true;
                if (mapping_node["struct_type"]) {
                    mapping.struct_type = mapping_node["struct_type"].as<std::string>();
                }
                if (mapping_node["struct_field"]) {
                    mapping.struct_field = mapping_node["struct_field"].as<std::string>();
                }
                VLOG(1) << "Signal " << signal_name << " maps to struct field " 
                        << mapping.vss_path << "." << mapping.struct_field;
            }
            
            // DAG support - optional fields
            if (mapping_node["provides"]) {
                mapping.provides = mapping_node["provides"].as<std::string>();
            }
            if (mapping_node["depends_on"]) {
                for (const auto& dep : mapping_node["depends_on"]) {
                    mapping.depends_on.push_back(dep.as<std::string>());
                }
            }
            
            // Parse transform
            if (mapping_node["transform"]) {
                const YAML::Node& transform = mapping_node["transform"];
                
                if (transform["code"]) {
                    mapping.transform = CodeTransform{transform["code"].as<std::string>()};
                } else if (transform["math"]) {
                    // Keep backward compatibility with old "math" keyword
                    mapping.transform = CodeTransform{transform["math"].as<std::string>()};
                } else if (transform["mapping"]) {
                    ValueMapping value_map;
                    for (const auto& item : transform["mapping"]) {
                        std::string from = item["from"].as<std::string>();
                        std::string to = item["to"].as<std::string>();
                        
                        // Resolve enum value if DBC parser is provided
                        std::string resolved_from = resolve_enum_value(dbc_signal_name, from, dbc_parser);
                        value_map.mappings[resolved_from] = to;
                    }
                    mapping.transform = value_map;
                } else {
                    mapping.transform = DirectMapping{};
                }
            } else {
                mapping.transform = DirectMapping{};
            }
            
            // Store mapping using DBC signal name for DBC signals,
            // or logical signal name for derived signals
            if (mapping_node["dbc_name"]) {
                signal_mappings_[dbc_signal_name] = mapping;
                // Also store by logical name for dependency resolution
                if (!mapping.provides.empty()) {
                    signal_mappings_[signal_name] = mapping;
                }
            } else {
                signal_mappings_[signal_name] = mapping;
            }
        }
        
        LOG(INFO) << "Loaded " << signal_mappings_.size() << " signal mappings from YAML";
        
        // Generate the base Lua infrastructure
        std::string base_lua = R"(
-- Signal transform functions table
transform_functions = {}

-- Helper function to create VSS signal
function create_vss_signal(path, value, datatype)
    return {
        path = path,
        value = value,
        type = datatype
    }
end

-- Process a single CAN signal
function process_signal(signal_name, value)
    local transform_func = transform_functions[signal_name]
    if transform_func then
        return transform_func(value)
    end
    return nil
end
)";
        
        if (!lua_mapper_->execute_lua_string(base_lua)) {
            LOG(ERROR) << "Failed to initialize Lua base infrastructure";
            return false;
        }
        
        // Generate transform functions for each mapping
        for (const auto& [signal_name, mapping] : signal_mappings_) {
            generate_lua_transform(signal_name, mapping);
        }
        
        return true;
        
    } catch (const YAML::Exception& e) {
        LOG(ERROR) << "Failed to parse YAML file: " << e.what();
        return false;
    }
}

void SignalMapper::generate_lua_transform(const std::string& signal_name, const SignalMapping& mapping) {
    std::string lua_code;
    
    if (std::holds_alternative<DirectMapping>(mapping.transform)) {
        lua_code = generate_direct_lua(signal_name, mapping);
    } else if (std::holds_alternative<CodeTransform>(mapping.transform)) {
        lua_code = generate_code_lua(signal_name, mapping, std::get<CodeTransform>(mapping.transform));
    } else if (std::holds_alternative<ValueMapping>(mapping.transform)) {
        lua_code = generate_mapping_lua(signal_name, mapping, std::get<ValueMapping>(mapping.transform));
    }
    
    if (!lua_mapper_->execute_lua_string(lua_code)) {
        LOG(ERROR) << "Failed to generate Lua transform for signal: " << signal_name;
    } else {
        VLOG(2) << "Generated transform for " << signal_name << " -> " << mapping.vss_path;
    }
}

std::string SignalMapper::generate_direct_lua(const std::string& signal_name, const SignalMapping& mapping) {
    std::stringstream lua;
    lua << "transform_functions['" << signal_name << "'] = function(value)\n";
    lua << "    return create_vss_signal('" << mapping.vss_path << "', value, '" << mapping.datatype << "')\n";
    lua << "end\n";
    return lua.str();
}

std::string SignalMapper::generate_code_lua(const std::string& signal_name, const SignalMapping& mapping, const CodeTransform& transform) {
    std::stringstream lua;
    lua << "transform_functions['" << signal_name << "'] = function(value)\n";
    lua << "    local x = value  -- Variable for single-line expressions\n";
    lua << "    local result = " << transform.expression << "\n";
    lua << "    return create_vss_signal('" << mapping.vss_path << "', result, '" << mapping.datatype << "')\n";
    lua << "end\n";
    return lua.str();
}

std::string SignalMapper::generate_mapping_lua(const std::string& signal_name, const SignalMapping& mapping, const ValueMapping& transform) {
    std::stringstream lua;
    
    lua << "transform_functions['" << signal_name << "'] = function(value)\n";
    lua << "    local mapping_table = {\n";
    
    for (const auto& [from, to] : transform.mappings) {
        lua << "        ['" << from << "'] = ";
        
        // Handle boolean values
        if (to == "true" || to == "false") {
            lua << to;
        } else {
            // Try to parse as number, otherwise treat as string
            try {
                double num = std::stod(to);
                lua << num;
            } catch (...) {
                lua << "'" << to << "'";
            }
        }
        
        lua << ",\n";
    }
    
    lua << "    }\n";
    
    // For numeric values, check if they map to string keys
    lua << "    local mapped_value = mapping_table[tostring(value)]\n";
    lua << "    if mapped_value == nil and type(value) == 'number' then\n";
    lua << "        -- Try numeric mapping for enums\n";
    lua << "        for k, v in pairs(mapping_table) do\n";
    lua << "            if tonumber(k) == value then\n";
    lua << "                mapped_value = v\n";
    lua << "                break\n";
    lua << "            end\n";
    lua << "        end\n";
    lua << "    end\n";
    lua << "    if mapped_value ~= nil then\n";
    lua << "        return create_vss_signal('" << mapping.vss_path << "', mapped_value, '" << mapping.datatype << "')\n";
    lua << "    end\n";
    lua << "    return nil\n";
    lua << "end\n";
    
    return lua.str();
}

std::vector<VSSSignal> SignalMapper::process_signals(const std::vector<std::pair<std::string, double>>& can_signals) {
    std::vector<VSSSignal> vss_signals;
    auto now = std::chrono::steady_clock::now();
    
    for (const auto& [signal_name, value] : can_signals) {
        // Fast lookup - only process signals we have mappings for
        auto mapping_it = signal_mappings_.find(signal_name);
        if (mapping_it == signal_mappings_.end()) {
            continue;
        }
        
        const SignalMapping& mapping = mapping_it->second;
        
        lua_mapper_->set_can_signal_value(signal_name, value);
        
        // Call the specific transform function
        auto result = lua_mapper_->call_transform_function(signal_name, value);
        if (!result.has_value()) {
            continue;
        }
        
        const VSSSignal& transformed_signal = result.value();
        const std::string& vss_path = transformed_signal.path;
        
        // Handle struct signals differently
        if (mapping.is_struct) {
            // Get or create struct buffer for this VSS path
            auto& buffer = struct_buffers_[vss_path];
            if (buffer.struct_type.empty()) {
                buffer.struct_type = mapping.struct_type;
                buffer.creation_time = now;
            }
            
            // Store the field value
            buffer.field_values[mapping.struct_field] = transformed_signal.value;
            buffer.field_set[mapping.struct_field] = true;
            
            VLOG(3) << "Updated struct field " << mapping.struct_field 
                    << " for " << vss_path << " = " << transformed_signal.value;
            
            // Continue to next signal - we'll check for complete structs later
            continue;
        }
        
        // Check if we should output based on interval (for non-struct signals)
        bool should_output = false;
        
        auto timing_it = output_timing_.find(vss_path);
        if (timing_it == output_timing_.end()) {
            // First time seeing this signal
            should_output = true;
            output_timing_[vss_path] = {now, transformed_signal.value};
        } else {
            OutputTiming& timing = timing_it->second;
            
            // Check if interval has elapsed
            if (mapping.interval_ms > 0) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - timing.last_output).count();
                
                if (elapsed_ms >= mapping.interval_ms) {
                    should_output = true;
                    timing.last_output = now;
                    timing.last_value = transformed_signal.value;
                }
            } else {
                // No interval specified, output on change
                if (timing.last_value != transformed_signal.value) {
                    should_output = true;
                    timing.last_output = now;
                    timing.last_value = transformed_signal.value;
                }
            }
        }
        
        if (should_output) {
            vss_signals.push_back(transformed_signal);
        }
    }
    
    // Check for complete structs
    for (auto& [vss_path, buffer] : struct_buffers_) {
        // Find all fields that should be in this struct
        std::vector<std::string> required_fields;
        for (const auto& [signal_name, mapping] : signal_mappings_) {
            if (mapping.is_struct && mapping.vss_path == vss_path) {
                required_fields.push_back(mapping.struct_field);
            }
        }
        
        // Check if struct is complete
        if (buffer.is_complete(required_fields)) {
            // Check timing for struct emission
            auto timing_it = output_timing_.find(vss_path);
            bool should_emit = false;
            
            if (timing_it == output_timing_.end()) {
                should_emit = true;
                output_timing_[vss_path] = {now, ""};
            } else {
                // Find any mapping for this struct to get interval_ms
                int interval_ms = 100;  // default
                for (const auto& [_, mapping] : signal_mappings_) {
                    if (mapping.is_struct && mapping.vss_path == vss_path) {
                        interval_ms = mapping.interval_ms;
                        break;
                    }
                }
                
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - timing_it->second.last_output).count();
                if (elapsed_ms >= interval_ms) {
                    should_emit = true;
                    timing_it->second.last_output = now;
                }
            }
            
            if (should_emit) {
                // Create JSON struct value
                std::string json_value = "{";
                bool first = true;
                for (const auto& [field, value] : buffer.field_values) {
                    if (!first) json_value += ",";
                    json_value += "\"" + field + "\":";
                    
                    // Check if value is a string that needs quotes
                    if (!value.empty() && (value[0] == '"' || 
                        std::all_of(value.begin(), value.end(), 
                            [](char c) { return std::isdigit(c) || c == '.' || c == '-'; }))) {
                        json_value += value;  // Already quoted or is a number
                    } else {
                        json_value += "\"" + value + "\"";  // Add quotes for string
                    }
                    first = false;
                }
                json_value += "}";
                
                VSSSignal struct_signal;
                struct_signal.path = vss_path;
                struct_signal.value_type = "struct";
                struct_signal.value = json_value;
                
                vss_signals.push_back(struct_signal);
                
                LOG(INFO) << "Emitted struct " << vss_path << ": " << json_value;
                
                // Clear the buffer after emission
                buffer.clear();
            }
        }
    }
    
    return vss_signals;
}

std::vector<std::string> SignalMapper::get_mapped_signals() const {
    std::vector<std::string> signals;
    signals.reserve(signal_mappings_.size());
    for (const auto& [signal_name, _] : signal_mappings_) {
        signals.push_back(signal_name);
    }
    return signals;
}

std::string SignalMapper::resolve_enum_value(const std::string& signal_name, const std::string& enum_string, 
                                             const DBCParser* dbc_parser) const {
    // If no DBC parser provided, return as-is (assume it's a numeric value)
    if (!dbc_parser) {
        return enum_string;
    }
    
    // If it's already a number, return as-is
    try {
        std::stoi(enum_string);
        return enum_string;
    } catch (...) {
        // Not a number, try to resolve as enum
    }
    
    // Get enum mappings for this signal
    auto enum_map = dbc_parser->get_signal_enums(signal_name);
    if (enum_map.empty()) {
        // No enums defined for this signal, check if it looks like an enum
        if (enum_string.find('_') != std::string::npos || 
            std::all_of(enum_string.begin(), enum_string.end(), 
                       [](char c) { return std::isupper(c) || c == '_' || std::isdigit(c); })) {
            LOG(ERROR) << "Signal '" << signal_name << "' has no enum definitions in DBC, "
                      << "but '" << enum_string << "' looks like an enum value";
            throw std::runtime_error("Enum value not found in DBC");
        }
        return enum_string;
    }
    
    // Look up the enum value
    auto it = enum_map.find(enum_string);
    if (it != enum_map.end()) {
        return std::to_string(it->second);
    }
    
    // Enum not found
    LOG(ERROR) << "Enum value '" << enum_string << "' not found for signal '" << signal_name << "'";
    LOG(ERROR) << "Valid enum values are:";
    for (const auto& [name, value] : enum_map) {
        LOG(ERROR) << "  " << value << " = " << name;
    }
    throw std::runtime_error("Invalid enum value in YAML mapping");
}

} // namespace can_to_vss
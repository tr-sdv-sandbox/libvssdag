#include "libVSSDAG/signal_processor_dag.h"
#include <glog/logging.h>
#include <sstream>
#include <iomanip>

namespace can_to_vss {

SignalProcessorDAG::SignalProcessorDAG() 
    : dag_(std::make_unique<SignalDAG>()),
      lua_mapper_(std::make_unique<LuaMapper>()),
      dbc_parser_(nullptr) {
}

SignalProcessorDAG::~SignalProcessorDAG() = default;

bool SignalProcessorDAG::initialize(const std::unordered_map<std::string, SignalMapping>& mappings,
                                   const DBCParser* dbc_parser) {
    dbc_parser_ = dbc_parser;
    
    // Build the DAG
    if (!dag_->build(mappings)) {
        LOG(ERROR) << "Failed to build signal DAG";
        return false;
    }
    
    // Set up Lua environment
    if (!setup_lua_environment()) {
        LOG(ERROR) << "Failed to setup Lua environment";
        return false;
    }
    
    // Generate transform functions for all nodes
    for (const auto* node : dag_->get_processing_order()) {
        generate_transform_function(node);
    }
    
    return true;
}

bool SignalProcessorDAG::setup_lua_environment() {
    const char* dag_lua_infrastructure = R"(
-- Signal values (read-only except through provide())
signal_values = {}

-- Signal states (private to each signal)
signal_states = {}

-- Current signal context
_current_signal = nil
_current_provides = nil

-- Dependencies for current signal
deps = {}

-- Create VSS signal
function create_vss_signal(path, value, datatype)
    -- Clean up float values to avoid displaying noise
    if (datatype == "float" or datatype == "double") and type(value) == "number" then
        if math.abs(value) < 1e-6 then
            value = 0
        end
    end
    
    return {
        path = path,
        value = value,
        type = datatype
    }
end

-- Get own state (each signal has private state)
function get_state()
    if not _current_signal then
        error("get_state() called outside signal context")
    end
    signal_states[_current_signal] = signal_states[_current_signal] or {}
    return signal_states[_current_signal]
end

-- Provide value (only allowed to set own provided value)
function provide(value)
    if not _current_provides then
        error("provide() called outside signal context")
    end
    signal_values[_current_provides] = value
    return value
end

-- Stateful operations
function lowpass(value, alpha)
    local state = get_state()
    if state.lp == nil then
        state.lp = value
    else
        state.lp = alpha * value + (1 - alpha) * state.lp
        -- Clean up floating point noise
        if math.abs(state.lp) < 1e-6 then
            state.lp = 0
        end
    end
    return state.lp
end

function moving_avg(value, window)
    local state = get_state()
    state.ma_hist = state.ma_hist or {}
    state.ma_sum = state.ma_sum or 0
    
    table.insert(state.ma_hist, value)
    state.ma_sum = state.ma_sum + value
    
    if #state.ma_hist > window then
        state.ma_sum = state.ma_sum - state.ma_hist[1]
        table.remove(state.ma_hist, 1)
    end
    
    return state.ma_sum / #state.ma_hist
end

function derivative(value)
    local state = get_state()
    -- Use the current timestamp from the processing context
    local t = _current_time
    
    if state.d_last_v == nil then
        state.d_last_v = value
        state.d_last_t = t
        return 0
    end
    
    local dt = t - state.d_last_t
    local deriv = 0
    
    -- Only calculate derivative if enough time has passed
    if dt > 0.01 then  -- At least 10ms
        deriv = (value - state.d_last_v) / dt
        -- Clean up floating point noise
        if math.abs(deriv) < 1e-6 then
            deriv = 0
        end
        
        -- Update last values only when we calculate
        state.d_last_v = value
        state.d_last_t = t
    else
        -- Return last calculated derivative if not enough time passed
        deriv = state.d_last_deriv or 0
    end
    
    state.d_last_deriv = deriv
    return deriv
end

function median(value, window)
    local state = get_state()
    state.med_hist = state.med_hist or {}
    
    table.insert(state.med_hist, value)
    if #state.med_hist > window then
        table.remove(state.med_hist, 1)
    end
    
    local sorted = {}
    for i, v in ipairs(state.med_hist) do
        sorted[i] = v
    end
    table.sort(sorted)
    
    return sorted[math.floor(#sorted / 2) + 1] or value
end

function rate_limit(value, max_rate)
    local state = get_state()
    local t = os.clock()
    
    if state.rl_last_v == nil then
        state.rl_last_v = value
        state.rl_last_t = t
        return value
    end
    
    local dt = t - state.rl_last_t
    if dt > 0 then
        local max_change = max_rate * dt
        local change = value - state.rl_last_v
        
        if math.abs(change) > max_change then
            value = state.rl_last_v + (change > 0 and max_change or -max_change)
        end
    end
    
    state.rl_last_v = value
    state.rl_last_t = t
    
    return value
end

-- Utilities
function clamp(value, min, max)
    return math.max(min, math.min(max, value))
end

function clean_float(value)
    -- Clean up floating point noise
    if type(value) == "number" and math.abs(value) < 1e-6 then
        return 0
    end
    return value
end

function deadband(value, threshold)
    return math.abs(value) < threshold and 0 or value
end

function sustained_condition(condition, duration_ms)
    local state = get_state()
    local now = os.clock() * 1000
    
    if condition then
        if not state.sc_start then
            state.sc_start = now
        end
        return (now - state.sc_start) >= duration_ms
    else
        state.sc_start = nil
        return false
    end
end

function rising_edge(value)
    local state = get_state()
    local edge = value and not state.re_last
    state.re_last = value
    return edge
end

function falling_edge(value)
    local state = get_state()
    local edge = not value and state.fe_last
    state.fe_last = value
    return edge
end

-- Transform functions table
transform_functions = {}

-- Process signal with context
function process_signal(signal_name, value)
    local transform_func = transform_functions[signal_name]
    if transform_func then
        if type(transform_func) ~= "function" then
            error("Transform for " .. signal_name .. " is not a function but a " .. type(transform_func))
        end
        return transform_func(value)
    end
    return nil
end
)";

    return lua_mapper_->execute_lua_string(dag_lua_infrastructure);
}

void SignalProcessorDAG::generate_transform_function(const SignalNode* node) {
    std::stringstream lua;
    
    lua << "transform_functions['" << node->signal_name << "'] = function(value)\n";
    
    // For derived signals, value parameter is ignored
    if (!node->is_can_signal) {
        lua << "    -- Derived signal, dependencies in 'deps' table\n";
    }
    
    // Handle different transform types
    if (std::holds_alternative<CodeTransform>(node->mapping.transform)) {
        const auto& code = std::get<CodeTransform>(node->mapping.transform);
        
        if (node->is_can_signal) {
            lua << "    local x = value\n";
        }
        
        // Check if expression is multi-line
        if (code.expression.find('\n') != std::string::npos) {
            // Multi-line expression - wrap in a function to capture the result
            lua << "    local function eval_expression()\n";
            
            std::istringstream expr_stream(code.expression);
            std::string line;
            while (std::getline(expr_stream, line)) {
                if (!line.empty()) {
                    lua << "        " << line << "\n";  // Extra indent inside function
                }
            }
            
            lua << "    end\n";
            lua << "    local result = eval_expression()\n";
            
            // Provide if needed
            if (!node->provides.empty() && node->provides != node->signal_name) {
                lua << "    if result ~= nil then provide(result) end\n";
            }
            
            // Create VSS signal
            lua << "    if result ~= nil then\n";
            if (!node->mapping.vss_path.empty()) {
                lua << "        return create_vss_signal('" << node->mapping.vss_path 
                    << "', result, '" << node->mapping.datatype << "')\n";
            } else {
                lua << "        return create_vss_signal('Custom." << node->signal_name 
                    << "', result, '" << node->mapping.datatype << "')\n";
            }
            lua << "    end\n";
            lua << "    return nil\n";
        } else {
            // Single-line expression
            lua << "    local result = " << code.expression << "\n";
            
            // Only provide if this signal declares a provides value
            if (!node->provides.empty() && node->provides != node->signal_name) {
                lua << "    provide(result)\n";
            }
            
            if (!node->mapping.vss_path.empty()) {
                lua << "    return create_vss_signal('" << node->mapping.vss_path 
                    << "', result, '" << node->mapping.datatype << "')\n";
            } else {
                // Use a custom namespace for internal signals
                lua << "    return create_vss_signal('Custom." << node->signal_name 
                    << "', result, '" << node->mapping.datatype << "')\n";
            }
        }
            
    } else if (std::holds_alternative<ValueMapping>(node->mapping.transform)) {
        const auto& value_map = std::get<ValueMapping>(node->mapping.transform);
        
        lua << "    local mapping_table = {\n";
        for (const auto& [from, to] : value_map.mappings) {
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
        
        lua << "    local result = mapping_table[tostring(value)]\n";
        
        // Handle numeric keys
        lua << "    if result == nil and type(value) == 'number' then\n";
        lua << "        for k, v in pairs(mapping_table) do\n";
        lua << "            if tonumber(k) == value then\n";
        lua << "                result = v\n";
        lua << "                break\n";
        lua << "            end\n";
        lua << "        end\n";
        lua << "    end\n";
        
        // Provide if needed
        if (!node->provides.empty() && node->provides != node->signal_name) {
            lua << "    if result ~= nil then provide(result) end\n";
        }
        
        lua << "    if result ~= nil then\n";
        if (!node->mapping.vss_path.empty()) {
            lua << "        return create_vss_signal('" << node->mapping.vss_path 
                << "', result, '" << node->mapping.datatype << "')\n";
        } else {
            lua << "        return create_vss_signal('Custom." << node->signal_name 
                << "', result, '" << node->mapping.datatype << "')\n";
        }
        lua << "    end\n";
        lua << "    return nil\n";
        
    } else {
        // DirectMapping
        if (node->is_can_signal) {
            lua << "    local result = value\n";
        } else {
            lua << "    local result = nil  -- DirectMapping not valid for derived signals\n";
        }
        
        if (!node->provides.empty() && node->provides != node->signal_name) {
            lua << "    provide(result)\n";
        }
        
        if (!node->mapping.vss_path.empty()) {
            lua << "    return create_vss_signal('" << node->mapping.vss_path 
                << "', result, '" << node->mapping.datatype << "')\n";
        } else {
            lua << "    return create_vss_signal('Custom." << node->signal_name 
                << "', result, '" << node->mapping.datatype << "')\n";
        }
    }
    
    lua << "end\n";
    
    if (!lua_mapper_->execute_lua_string(lua.str())) {
        LOG(ERROR) << "Failed to generate transform for signal: " << node->signal_name;
    } else {
        VLOG(2) << "Generated transform for " << node->signal_name;
    }
}

std::vector<VSSSignal> SignalProcessorDAG::process_can_signals(
    const std::vector<std::pair<std::string, double>>& can_signals) {
    
    std::vector<VSSSignal> vss_signals;
    
    // Update CAN signal values and mark nodes as updated
    for (const auto& [signal_name, value] : can_signals) {
        if (auto* node = dag_->get_node(signal_name)) {
            if (node->is_can_signal) {
                VLOG(2) << "Updating CAN signal " << signal_name << " = " << value 
                        << " (provides: " << node->provides << ")";
                // Store the value
                signal_values_[node->provides] = value;
                node->last_value = value;
                node->last_update = std::chrono::steady_clock::now();
                
                // Mark this node and its dependents as having new data
                dag_->mark_can_signal_updated(signal_name);
            }
        } else {
            VLOG(3) << "Ignoring unknown CAN signal: " << signal_name;
        }
    }
    
    // First pass: Mark nodes that need processing (data updates + periodic triggers)
    auto now = std::chrono::steady_clock::now();
    std::vector<SignalNode*> nodes_to_process;
    
    VLOG(3) << "Checking " << dag_->get_processing_order().size() << " nodes for processing";
    
    for (auto* node : dag_->get_processing_order()) {
        bool needs_processing = false;
        
        // Check if node has new data from dependencies
        if (node->has_new_data) {
            needs_processing = true;
            VLOG(3) << "Node " << node->signal_name << " has new data";
        }
        
        // Check if node needs periodic processing
        if (node->mapping.update_trigger == UpdateTrigger::PERIODIC || 
            node->mapping.update_trigger == UpdateTrigger::BOTH) {
            
            if (node->mapping.interval_ms > 0) {
                // Check if all dependencies have values before allowing periodic processing
                bool deps_available = true;
                for (const auto& dep : node->depends_on) {
                    if (signal_values_.find(dep) == signal_values_.end()) {
                        deps_available = false;
                        break;
                    }
                }
                
                if (deps_available) {
                    // Handle first time - if last_process is min, always process
                    if (node->last_process == std::chrono::steady_clock::time_point::min()) {
                        needs_processing = true;
                        node->needs_periodic_update = true;
                    } else {
                        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - node->last_process).count();
                        
                        if (elapsed_ms >= node->mapping.interval_ms) {
                            needs_processing = true;
                            node->needs_periodic_update = true;
                        }
                    }
                }
            }
        }
        
        if (needs_processing) {
            VLOG(3) << "Node " << node->signal_name << " needs processing"
                    << " (has_new_data=" << node->has_new_data 
                    << ", periodic=" << node->needs_periodic_update << ")";
            nodes_to_process.push_back(node);
            // Mark dependents as potentially needing update
            for (auto* dependent : node->dependents) {
                dependent->has_new_data = true;
            }
        }
    }
    
    // Second pass: Process all marked nodes in topological order
    VLOG(2) << "Processing " << nodes_to_process.size() << " nodes";
    for (auto* node : dag_->get_processing_order()) {
        if (std::find(nodes_to_process.begin(), nodes_to_process.end(), node) != nodes_to_process.end() ||
            node->has_new_data) {
            
            VLOG(3) << "Processing node: " << node->signal_name;
            auto result = process_node(node);
            
            // Update last process time if this was a periodic update
            if (node->needs_periodic_update) {
                node->last_process = now;
                node->needs_periodic_update = false;
            }
            
            if (result.has_value()) {
                VLOG(2) << "Node " << node->signal_name << " produced result: " 
                        << result.value().value;
                // Check interval throttling for output
                bool should_output = false;
                
                // Handle first output - if last_output is min, always output
                if (node->last_output == std::chrono::steady_clock::time_point::min()) {
                    should_output = true;
                } else {
                    auto elapsed_output_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - node->last_output).count();
                    
                    if (node->mapping.interval_ms > 0) {
                        if (elapsed_output_ms >= node->mapping.interval_ms) {
                            should_output = true;
                        }
                    } else {
                        // No interval specified, always output
                        should_output = true;
                    }
                }
                
                if (should_output) {
                    vss_signals.push_back(result.value());
                    
                    if (node->last_output == std::chrono::steady_clock::time_point::min()) {
                        VLOG(2) << "Output signal " << node->signal_name << " (first output)";
                    } else {
                        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - node->last_output).count();
                        VLOG(2) << "Output signal " << node->signal_name << " after " << elapsed_ms << "ms";
                    }
                    
                    node->last_output = now;
                    node->last_output_value = result.value().value;
                } else {
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - node->last_output).count();
                    VLOG(2) << "Throttled signal " << node->signal_name << " (" << elapsed_ms << "ms < " << node->mapping.interval_ms << "ms)";
                }
            }
            node->has_new_data = false;
        }
    }
    
    return vss_signals;
}

std::optional<VSSSignal> SignalProcessorDAG::process_node(SignalNode* node) {
    // Set up context
    setup_node_context(node);
    
    // Get input value
    double input_value = 0.0;
    if (node->is_can_signal) {
        // For CAN signals, use the last value
        input_value = node->last_value;
    }
    // For derived signals, input_value is ignored
    
    // Call transform function
    lua_mapper_->set_can_signal_value(node->signal_name, input_value);
    auto result = lua_mapper_->call_transform_function(node->signal_name, input_value);
    
    // Update provided value if transform succeeded
    if (result.has_value()) {
        if (!node->provides.empty()) {
            // Get the provided value from Lua
            auto provided_value = lua_mapper_->get_lua_variable("signal_values." + node->provides);
            if (provided_value.has_value()) {
                try {
                    signal_values_[node->provides] = std::stod(provided_value.value());
                } catch (...) {
                    // Value might be a string, store as 0 for now
                    signal_values_[node->provides] = 0.0;
                }
            }
        }
    }
    
    return result;
}

void SignalProcessorDAG::setup_node_context(const SignalNode* node) {
    // Set current signal context
    std::stringstream context;
    context << "_current_signal = '" << node->signal_name << "'\n";
    context << "_current_provides = '" << node->provides << "'\n";
    
    // Set current timestamp (seconds since epoch with microsecond precision)
    auto now = std::chrono::steady_clock::now();
    auto epoch = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(epoch).count();
    context << "_current_time = " << std::fixed << std::setprecision(6) << seconds << "\n";
    
    // Clear and set dependencies
    context << "deps = {}\n";
    
    for (const auto& dep : node->depends_on) {
        if (auto* provider = dag_->get_provider(dep)) {
            auto it = signal_values_.find(dep);
            if (it != signal_values_.end()) {
                context << "deps." << dep << " = " << it->second << "\n";
            } else {
                // Explicitly set nil for missing dependencies
                context << "deps." << dep << " = nil\n";
            }
        }
    }
    
    lua_mapper_->execute_lua_string(context.str());
}

std::vector<std::string> SignalProcessorDAG::get_required_can_signals() const {
    std::vector<std::string> signals;
    
    for (const auto& node : dag_->get_nodes()) {
        if (node->is_can_signal) {
            signals.push_back(node->signal_name);
        }
    }
    
    return signals;
}

} // namespace can_to_vss
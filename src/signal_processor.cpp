#include "vssdag/signal_processor.h"
#include <glog/logging.h>
#include <sstream>
#include <iomanip>

namespace vssdag {

SignalProcessorDAG::SignalProcessorDAG() 
    : dag_(std::make_unique<SignalDAG>()),
      lua_mapper_(std::make_unique<LuaMapper>()) {
}

SignalProcessorDAG::~SignalProcessorDAG() = default;

bool SignalProcessorDAG::initialize(const std::unordered_map<std::string, SignalMapping>& mappings) {
    
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
        if (!generate_transform_function(node)) {
            LOG(ERROR) << "Failed to generate transform for signal: " << node->signal_name;
            return false;  // FAIL FAST on Lua compilation errors
        }
    }

    return true;
}

bool SignalProcessorDAG::setup_lua_environment() {
    // First, set up ValueType enum constants from C++
    lua_State* L = lua_mapper_->get_lua_state();

    // ValueType enum constants
    lua_pushinteger(L, static_cast<int>(ValueType::UNSPECIFIED));
    lua_setglobal(L, "TYPE_UNSPECIFIED");
    lua_pushinteger(L, static_cast<int>(ValueType::STRING));
    lua_setglobal(L, "TYPE_STRING");
    lua_pushinteger(L, static_cast<int>(ValueType::BOOL));
    lua_setglobal(L, "TYPE_BOOL");
    lua_pushinteger(L, static_cast<int>(ValueType::INT32));
    lua_setglobal(L, "TYPE_INT32");
    lua_pushinteger(L, static_cast<int>(ValueType::INT64));
    lua_setglobal(L, "TYPE_INT64");
    lua_pushinteger(L, static_cast<int>(ValueType::UINT32));
    lua_setglobal(L, "TYPE_UINT32");
    lua_pushinteger(L, static_cast<int>(ValueType::UINT64));
    lua_setglobal(L, "TYPE_UINT64");
    lua_pushinteger(L, static_cast<int>(ValueType::FLOAT));
    lua_setglobal(L, "TYPE_FLOAT");
    lua_pushinteger(L, static_cast<int>(ValueType::DOUBLE));
    lua_setglobal(L, "TYPE_DOUBLE");
    lua_pushinteger(L, static_cast<int>(ValueType::STRING_ARRAY));
    lua_setglobal(L, "TYPE_STRING_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::BOOL_ARRAY));
    lua_setglobal(L, "TYPE_BOOL_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::INT32_ARRAY));
    lua_setglobal(L, "TYPE_INT32_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::INT64_ARRAY));
    lua_setglobal(L, "TYPE_INT64_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::UINT32_ARRAY));
    lua_setglobal(L, "TYPE_UINT32_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::UINT64_ARRAY));
    lua_setglobal(L, "TYPE_UINT64_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::FLOAT_ARRAY));
    lua_setglobal(L, "TYPE_FLOAT_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::DOUBLE_ARRAY));
    lua_setglobal(L, "TYPE_DOUBLE_ARRAY");
    lua_pushinteger(L, static_cast<int>(ValueType::STRUCT));
    lua_setglobal(L, "TYPE_STRUCT");
    lua_pushinteger(L, static_cast<int>(ValueType::STRUCT_ARRAY));
    lua_setglobal(L, "TYPE_STRUCT_ARRAY");

    const char* dag_lua_infrastructure = R"(
-- Signal status constants (matching vss::types::SignalQuality enum)
STATUS_UNKNOWN = 0
STATUS_VALID = 1
STATUS_INVALID = 2
STATUS_NOT_AVAILABLE = 3
STATUS_STALE = 4
STATUS_OUT_OF_RANGE = 5

-- Invalid signal handling strategies
STRATEGY_PROPAGATE = 0     -- Return nil immediately (default)
STRATEGY_HOLD = 1          -- Return last valid value
STRATEGY_HOLD_TIMEOUT = 2  -- Return last valid for a period, then nil

-- Default timeout for STRATEGY_HOLD_TIMEOUT (in seconds)
DEFAULT_HOLD_TIMEOUT = 5.0

-- Signal values (read-only except through provide())
signal_values = {}

-- Signal status tracking (uses integer status constants)
signal_status = {}

-- Signal states (private to each signal)
signal_states = {}

-- Signals with pending time-based operations (like delayed())
signals_pending_reevaluation = {}

-- Current signal context
_current_signal = nil
_current_provides = nil

-- Dependencies for current signal
deps = {}
deps_status = {}

-- Create VSS signal
function create_vss_signal(path, value, datatype, status)
    -- datatype is now an integer (ValueType enum value from C++)
    -- Default status to STATUS_VALID if not provided
    status = status or STATUS_VALID

    -- If value is nil, set status to invalid if not already set
    if value == nil and status == STATUS_VALID then
        status = STATUS_INVALID
    end

    -- Clean up float values to avoid displaying noise
    if (datatype == TYPE_FLOAT or datatype == TYPE_DOUBLE) and type(value) == "number" then
        if math.abs(value) < 1e-6 then
            value = 0
        end
    end

    return {
        path = path,
        value = value,
        type = datatype,  -- Stores integer enum value
        status = status
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

-- Mark signal as needing re-evaluation (for time-based operations)
function mark_pending()
    if not _current_signal then
        error("mark_pending() called outside signal context")
    end
    signals_pending_reevaluation[_current_signal] = true
end

function clear_pending()
    if not _current_signal then
        error("clear_pending() called outside signal context")
    end
    signals_pending_reevaluation[_current_signal] = nil
end

-- Provide value (only allowed to set own provided value)
function provide(value)
    if not _current_signal then
        error("provide() called outside signal context")
    end
    -- Use bracket notation for signal names with dots
    signal_values[_current_signal] = value
    return value
end

-- Stateful operations
function lowpass(value, alpha, invalid_strategy)
    -- invalid_strategy: STRATEGY_PROPAGATE (0), STRATEGY_HOLD (1), or STRATEGY_HOLD_TIMEOUT (2)
    -- Default to STRATEGY_PROPAGATE if not specified
    invalid_strategy = invalid_strategy or STRATEGY_PROPAGATE
    
    if value == nil then
        local state = get_state()
        
        if invalid_strategy == STRATEGY_PROPAGATE then
            return nil
        elseif invalid_strategy == STRATEGY_HOLD then
            return state.last_valid_output
        elseif invalid_strategy == STRATEGY_HOLD_TIMEOUT then
            -- Check if we've been invalid too long
            if state.invalid_since == nil then
                state.invalid_since = _current_time
            end
            local invalid_duration = _current_time - state.invalid_since
            if invalid_duration < DEFAULT_HOLD_TIMEOUT then
                return state.last_valid_output
            else
                return nil  -- Been invalid too long
            end
        end
        return nil
    end
    
    local state = get_state()
    state.invalid_since = nil  -- Clear invalid timer
    
    if state.lp == nil then
        state.lp = value
    else
        state.lp = alpha * value + (1 - alpha) * state.lp
        -- Clean up floating point noise
        if math.abs(state.lp) < 1e-6 then
            state.lp = 0
        end
    end
    state.last_valid_output = state.lp
    return state.lp
end

function moving_avg(value, window)
    if value == nil then
        -- Don't add nil to history
        local state = get_state()
        if state.ma_hist and #state.ma_hist > 0 then
            return state.ma_sum / #state.ma_hist  -- Return current average
        end
        return nil
    end
    
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
    if value == nil then
        -- Can't calculate derivative with nil
        return nil
    end
    
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

function delayed(value, delay_ms)
    local state = get_state()
    local now = _current_time

    -- Check if value has changed
    if state.delay_target_value ~= value then
        -- Value changed - start new delay timer
        state.delay_target_value = value
        state.delay_start_time = now
        state.delay_pending = true
        mark_pending()  -- Immediately mark for re-evaluation
    end

    -- Check if delay has elapsed
    if state.delay_pending then
        local elapsed_ms = (now - state.delay_start_time) * 1000  -- Convert seconds to milliseconds
        if elapsed_ms >= delay_ms then
            -- Delay elapsed - output the target value
            state.delay_output_value = state.delay_target_value
            state.delay_pending = false
            clear_pending()  -- No longer needs re-evaluation
        else
            -- Still waiting - keep marked for re-evaluation
            mark_pending()
        end
    end

    -- Return current output value (will be nil initially, then delayed value)
    return state.delay_output_value
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

bool SignalProcessorDAG::generate_transform_function(const SignalNode* node) {
    std::stringstream lua;
    
    lua << "transform_functions['" << node->signal_name << "'] = function(value)\n";
    
    // For derived signals, value parameter is ignored
    if (!node->is_input_signal) {
        lua << "    -- Derived signal, dependencies in 'deps' table\n";
    }
    
    // Handle different transform types
    if (std::holds_alternative<CodeTransform>(node->mapping.transform)) {
        const auto& code = std::get<CodeTransform>(node->mapping.transform);
        
        if (node->is_input_signal) {
            // Check signal status and set x to nil if invalid/NA
            lua << "    local x = value\n";
            lua << "    local my_status = signal_status['" << node->signal_name << "'] or STATUS_VALID\n";
            lua << "    if my_status ~= STATUS_VALID then\n";
            lua << "        x = nil\n";
            lua << "    end\n";
        } else {
            lua << "    local my_status = STATUS_VALID  -- Will be updated based on deps\n";
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
            lua << "    if result ~= nil then provide(result) end\n";
            
            // Create VSS signal with appropriate status
            // For input signals, my_status is already set; for derived signals, determine from result
            if (!node->is_input_signal) {
                lua << "    if result == nil then my_status = STATUS_INVALID end\n";
            }
            lua << "    return create_vss_signal('" << node->signal_name
                << "', result, " << static_cast<int>(node->mapping.datatype) << ", my_status)\n";
        } else {
            // Single-line expression
            lua << "    local result = " << code.expression << "\n";
            
            // Only provide if this signal declares a provides value
            lua << "    provide(result)\n";
            
            // For input signals, my_status is already set; for derived signals, determine from result
            if (!node->is_input_signal) {
                lua << "    if result == nil then my_status = STATUS_INVALID end\n";
            }
            lua << "    return create_vss_signal('" << node->signal_name
                << "', result, " << static_cast<int>(node->mapping.datatype) << ", my_status)\n";
        }
            
    } else if (std::holds_alternative<ValueMapping>(node->mapping.transform)) {
        const auto& value_map = std::get<ValueMapping>(node->mapping.transform);
        
        // Set up status tracking
        if (node->is_input_signal) {
            lua << "    local my_status = signal_status['" << node->signal_name << "'] or STATUS_VALID\n";
        } else {
            lua << "    local my_status = STATUS_VALID\n";
        }
        
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
        
        // Always provide the result
        lua << "    if result ~= nil then provide(result) end\n";
        
        // For derived signals, set invalid if result is nil
        if (!node->is_input_signal) {
            lua << "    if result == nil then my_status = 'invalid' end\n";
        }
        lua << "    return create_vss_signal('" << node->signal_name
            << "', result, " << static_cast<int>(node->mapping.datatype) << ", my_status)\n";
        
    } else {
        // DirectMapping
        if (node->is_input_signal) {
            lua << "    local result = value\n";
            lua << "    local my_status = signal_status['" << node->signal_name << "'] or STATUS_VALID\n";
            lua << "    if my_status ~= STATUS_VALID then\n";
            lua << "        result = nil\n";
            lua << "    end\n";
        } else {
            lua << "    local result = nil  -- DirectMapping not valid for derived signals\n";
            lua << "    local my_status = STATUS_INVALID\n";
        }
        
        lua << "    provide(result)\n";
        
        // For input signals, use the actual status; for derived, it's already set above
        if (node->is_input_signal) {
            lua << "    -- Status already set from signal_status table\n";
        }
        lua << "    return create_vss_signal('" << node->signal_name
            << "', result, " << static_cast<int>(node->mapping.datatype) << ", my_status)\n";
    }
    
    lua << "end\n";

    std::string lua_code = lua.str();
    if (!lua_mapper_->execute_lua_string(lua_code)) {
        LOG(ERROR) << "Failed to execute Lua transform for signal: " << node->signal_name;
        return false;
    }

    VLOG(2) << "Generated transform for " << node->signal_name;
    return true;
}

// process_can_signals method removed - functionality merged into process_signal_updates

std::optional<VSSSignal> SignalProcessorDAG::process_node(SignalNode* node) {
    // Set up context
    setup_node_context(node);
    
    // Get input value - now typed
    std::variant<int64_t, double, std::string> input_value;
    if (node->is_input_signal) {
        auto it = signal_values_.find(node->signal_name);
        if (it != signal_values_.end() &&
            it->second.quality != vss::types::SignalQuality::VALID) {
            // For invalid/NA signals, we'll pass a special marker value
            // The Lua transform can check for nil and decide what to do
            input_value = 0.0;  // Dummy value, Lua will see nil
        } else if (it != signal_values_.end()) {
            // For valid input signals, use the stored typed value
            // Extract the value from the qualified value - handle all types
            if (auto* val = std::get_if<bool>(&it->second.value)) {
                input_value = *val;
            } else if (auto* val = std::get_if<int8_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<int16_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<int32_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<int64_t>(&it->second.value)) {
                input_value = *val;
            } else if (auto* val = std::get_if<uint8_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<uint16_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<uint32_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<uint64_t>(&it->second.value)) {
                input_value = static_cast<int64_t>(*val);
            } else if (auto* val = std::get_if<float>(&it->second.value)) {
                input_value = static_cast<double>(*val);
            } else if (auto* val = std::get_if<double>(&it->second.value)) {
                input_value = *val;
            } else if (auto* val = std::get_if<std::string>(&it->second.value)) {
                input_value = *val;
            } else {
                input_value = 0.0;  // Default for other types
                LOG(WARNING) << "Could not extract value for " << node->signal_name << " - unhandled type in Value variant";
            }
        } else {
            // If not found, use default
            input_value = 0.0;
        }
    } else {
        // For derived signals, input is ignored but we need a value
        input_value = 0.0;
    }
    
    // Convert to double for Lua (temporary until Lua mapper is updated)
    double lua_input = std::visit([](auto&& val) -> double {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<double>(val);
        } else if constexpr (std::is_same_v<T, double>) {
            return val;
        } else {
            // String - try to convert, default to 0
            try {
                return std::stod(val);
            } catch (...) {
                return 0.0;
            }
        }
    }, input_value);
    
    // Set signal status in Lua for input signals
    if (node->is_input_signal) {
        lua_State* L = lua_mapper_->get_lua_state();
        lua_getglobal(L, "signal_status");
        if (lua_istable(L, -1)) {
            lua_pushstring(L, node->signal_name.c_str());

            auto it = signal_values_.find(node->signal_name);
            int status_val = 0;  // STATUS_VALID
            if (it != signal_values_.end()) {
                status_val = static_cast<int>(it->second.quality);
            }
            lua_pushinteger(L, status_val);
            lua_settable(L, -3);
        }
        lua_pop(L, 1);  // Pop signal_status table
    }
    
    // Call transform function
    lua_mapper_->set_can_signal_value(node->signal_name, lua_input);
    auto result = lua_mapper_->call_transform_function(node->signal_name, lua_input);
    
    // Update provided value if transform succeeded
    if (result.has_value()) {
        // Get the provided value from Lua
        auto provided_value = lua_mapper_->get_lua_variable("signal_values['" + node->signal_name + "']");
        if (provided_value.has_value()) {
            // Try to determine the type and store appropriately
            try {
                // Check if it's an integer
                double d = std::stod(provided_value.value());
                if (std::floor(d) == d && d >= std::numeric_limits<int64_t>::min() && d <= std::numeric_limits<int64_t>::max()) {
                    signal_values_[node->signal_name].value = static_cast<int64_t>(d);
                } else {
                    signal_values_[node->signal_name].value = d;
                }
            } catch (...) {
                // Store as string if conversion fails
                signal_values_[node->signal_name].value = provided_value.value();
            }
            signal_values_[node->signal_name].quality = SignalQuality::VALID;
            signal_values_[node->signal_name].timestamp = std::chrono::system_clock::now();
        }
    }
    
    return result;
}

void SignalProcessorDAG::setup_node_context(const SignalNode* node) {
    lua_State* L = lua_mapper_->get_lua_state();
    
    // Set current signal context
    lua_pushstring(L, node->signal_name.c_str());
    lua_setglobal(L, "_current_signal");
    
    // Set current timestamp (seconds since epoch with microsecond precision)
    auto now = std::chrono::steady_clock::now();
    auto epoch = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(epoch).count();
    lua_pushnumber(L, seconds);
    lua_setglobal(L, "_current_time");
    
    // Create deps table
    lua_newtable(L);
    
    for (const auto& dep : node->depends_on) {
        auto it = signal_values_.find(dep);
        // Push key
        lua_pushstring(L, dep.c_str());

        if (it != signal_values_.end() && it->second.quality == vss::types::SignalQuality::VALID) {
            // Push typed value only if quality is VALID
            VSSTypeHelper::push_value_to_lua(L, it->second.value);
        } else {
            // Push nil for invalid/unavailable/not found signals
            lua_pushnil(L);
        }

        // Set table
        lua_settable(L, -3);
    }
    
    // Set the deps table as global
    lua_setglobal(L, "deps");
    
    // Create deps_status table
    lua_newtable(L);

    for (const auto& dep : node->depends_on) {
        auto it = signal_values_.find(dep);
        if (it != signal_values_.end()) {
            lua_pushstring(L, dep.c_str());

            // Push status as integer matching Lua constants
            int status_val = static_cast<int>(it->second.quality);
            lua_pushinteger(L, status_val);
            lua_settable(L, -3);
        }
    }
    
    // Set the deps_status table as global
    lua_setglobal(L, "deps_status");
}

std::vector<std::string> SignalProcessorDAG::get_required_input_signals() const {
    std::vector<std::string> signals;
    
    for (const auto& node : dag_->get_nodes()) {
        if (node->is_input_signal) {
            signals.push_back(node->signal_name);
        }
    }
    
    return signals;
}

std::vector<VSSSignal> SignalProcessorDAG::process_signal_updates(
    const std::vector<vssdag::SignalUpdate>& updates) {
    
    std::vector<VSSSignal> vss_signals;
    
    // Update signal values and mark nodes as updated
    for (const auto& update : updates) {
        if (auto* node = dag_->get_node(update.signal_name)) {
            if (node->is_input_signal) {
                // Store the qualified value (value + quality + timestamp)
                signal_values_[update.signal_name].value = update.value;
                signal_values_[update.signal_name].quality = update.status;
                // Convert steady_clock to system_clock timestamp
                auto steady_now = std::chrono::steady_clock::now();
                auto system_now = std::chrono::system_clock::now();
                auto elapsed = steady_now - update.timestamp;
                signal_values_[update.signal_name].timestamp = system_now - elapsed;

                // Log the update
                if (update.status == vss::types::SignalQuality::VALID) {
                    // Log with type info
                    std::string value_str = VSSTypeHelper::to_string(update.value);
                    VLOG(2) << "Updating input signal " << update.signal_name << " = " << value_str;
                } else {
                    // Log invalid/not available status
                    VLOG(2) << "Updating input signal " << update.signal_name
                            << " status=" << (update.status == vss::types::SignalQuality::INVALID ? "Invalid" : "NotAvailable");
                }
                
                node->last_update = update.timestamp;
                
                // Mark this node and its dependents as having new data
                dag_->mark_can_signal_updated(update.signal_name);
            }
        } else {
            VLOG(3) << "Ignoring unknown signal: " << update.signal_name;
        }
    }
    
    // Process nodes (similar to process_can_signals but simplified)
    auto now = std::chrono::steady_clock::now();
    std::vector<SignalNode*> nodes_to_process;
    
    for (auto* node : dag_->get_processing_order()) {
        bool needs_processing = false;
        
        if (node->has_new_data) {
            needs_processing = true;
        }
        
        // Check periodic updates
        if (node->mapping.update_trigger == UpdateTrigger::PERIODIC || 
            node->mapping.update_trigger == UpdateTrigger::BOTH) {
            
            if (node->mapping.interval_ms > 0) {
                bool deps_available = true;
                for (const auto& dep : node->depends_on) {
                    if (signal_values_.find(dep) == signal_values_.end()) {
                        deps_available = false;
                        break;
                    }
                }
                
                if (deps_available) {
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
            nodes_to_process.push_back(node);
            for (auto* dependent : node->dependents) {
                dependent->has_new_data = true;
            }
        }
    }
    
    // Process nodes
    for (auto* node : dag_->get_processing_order()) {
        if (std::find(nodes_to_process.begin(), nodes_to_process.end(), node) != nodes_to_process.end() ||
            node->has_new_data) {
            
            auto result = process_node(node);
            
            if (node->needs_periodic_update) {
                node->last_process = now;
                node->needs_periodic_update = false;
            }
            
            if (result.has_value()) {
                bool should_output = false;
                
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
                        should_output = true;
                    }
                }
                
                if (should_output) {
                    vss_signals.push_back(result.value());
                    node->last_output = now;
                    node->last_output_value = VSSTypeHelper::to_string(result.value().qualified_value.value);
                }
            }
            node->has_new_data = false;
        }
    }

    // PHASE 2: Check for signals with pending time-based operations (like delayed())
    lua_State* L = lua_mapper_->get_lua_state();
    lua_getglobal(L, "signals_pending_reevaluation");
    if (lua_istable(L, -1)) {
        // Iterate through pending signals
        lua_pushnil(L);  // First key
        while (lua_next(L, -2) != 0) {
            // key is at index -2, value at index -1
            if (lua_isstring(L, -2)) {
                std::string signal_name = lua_tostring(L, -2);
                VLOG(2) << "Phase 2: Found pending signal: " << signal_name;

                // Find the node and re-evaluate it
                auto* node = dag_->get_node(signal_name);
                if (node && !node->is_input_signal) {
                    VLOG(2) << "Phase 2: Re-evaluating pending signal: " << signal_name;
                    auto result = process_node(node);

                    if (result.has_value()) {
                        // For phase 2 (deferred evaluation), only output if:
                        // 1. Signal becomes valid (delay elapsed)
                        // 2. Value changed
                        bool should_output = false;

                        if (result.value().qualified_value.is_valid()) {
                            // Signal is now valid - check if it changed
                            if (node->last_output == std::chrono::steady_clock::time_point::min()) {
                                // First valid output
                                should_output = true;
                                VLOG(1) << "Phase 2: First valid output for " << signal_name;
                            } else {
                                std::string new_value_str = VSSTypeHelper::to_string(result.value().qualified_value.value);
                                if (node->last_output_value != new_value_str) {
                                    should_output = true;
                                    VLOG(1) << "Phase 2: Value changed for " << signal_name;
                                }
                            }

                            if (should_output) {
                                vss_signals.push_back(result.value());
                                node->last_output = now;
                                node->last_output_value = VSSTypeHelper::to_string(result.value().qualified_value.value);
                                VLOG(1) << "Phase 2: Publishing output for " << signal_name;
                            }
                        }
                    }
                }
            }
            lua_pop(L, 1);  // Remove value, keep key for next iteration
        }
    }
    lua_pop(L, 1);  // Pop the table

    return vss_signals;
}

} // namespace vssdag
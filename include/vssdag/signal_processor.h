#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <variant>
#include "vssdag/signal_dag.h"
#include "vssdag/lua_mapper.h"
#include "vssdag/signal_source.h"

namespace vssdag {

class SignalProcessorDAG {
public:
    SignalProcessorDAG();
    ~SignalProcessorDAG();
    
    // Initialize with mappings
    bool initialize(const std::unordered_map<std::string, SignalMapping>& mappings);
    
    // Process signal updates from signal sources
    std::vector<VSSSignal> process_signal_updates(
        const std::vector<vssdag::SignalUpdate>& updates);
    
    // Get list of input signals we're interested in
    std::vector<std::string> get_required_input_signals() const;

private:
    std::unique_ptr<SignalDAG> dag_;
    std::unique_ptr<LuaMapper> lua_mapper_;

    // Current qualified values for all provided signals (combines value + quality + timestamp)
    std::unordered_map<std::string, DynamicQualifiedValue> signal_values_;

    // Track last processing time for periodic updates
    std::chrono::steady_clock::time_point last_periodic_check_;
    
    // Generate Lua infrastructure
    bool setup_lua_environment();
    
    // Generate transform function for a node
    bool generate_transform_function(const SignalNode* node);
    
    // Process a single node
    std::optional<VSSSignal> process_node(SignalNode* node);
    
    // Set up Lua context for a node
    void setup_node_context(const SignalNode* node);
};

} // namespace vssdag
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include "signal_dag.h"
#include "lua_mapper.h"
#include "dbc_parser.h"

namespace can_to_vss {

class SignalProcessorDAG {
public:
    SignalProcessorDAG();
    ~SignalProcessorDAG();
    
    // Initialize with mappings and optional DBC parser for enum support
    bool initialize(const std::unordered_map<std::string, SignalMapping>& mappings,
                   const DBCParser* dbc_parser = nullptr);
    
    // Process CAN signals and return VSS signals
    std::vector<VSSSignal> process_can_signals(
        const std::vector<std::pair<std::string, double>>& can_signals);
    
    // Get list of CAN signals we're interested in
    std::vector<std::string> get_required_can_signals() const;

private:
    std::unique_ptr<SignalDAG> dag_;
    std::unique_ptr<LuaMapper> lua_mapper_;
    const DBCParser* dbc_parser_;
    
    // Current values for all provided signals
    std::unordered_map<std::string, double> signal_values_;
    
    // Track last processing time for periodic updates
    std::chrono::steady_clock::time_point last_periodic_check_;
    
    // Generate Lua infrastructure
    bool setup_lua_environment();
    
    // Generate transform function for a node
    void generate_transform_function(const SignalNode* node);
    
    // Process a single node
    std::optional<VSSSignal> process_node(SignalNode* node);
    
    // Set up Lua context for a node
    void setup_node_context(const SignalNode* node);
};

} // namespace can_to_vss
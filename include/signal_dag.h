#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <stdexcept>
#include <queue>
#include <glog/logging.h>
#include "signal_mapper.h"

namespace can_to_vss {

// Forward declaration
struct SignalMapping;

// Signal node in the DAG
struct SignalNode {
    std::string signal_name;       // CAN signal name or synthetic name
    std::string provides;          // Value name this signal provides
    std::vector<std::string> depends_on;  // Value names this depends on
    std::vector<SignalNode*> dependents;  // Nodes that depend on this
    
    // For topological sort
    int in_degree = 0;
    bool is_can_signal = true;     // false for derived signals
    
    // Transform configuration
    SignalMapping mapping;
    
    // Runtime state
    bool has_new_data = false;
    double last_value = 0.0;
    std::chrono::steady_clock::time_point last_update;
    
    // Output throttling
    std::chrono::steady_clock::time_point last_output = std::chrono::steady_clock::time_point::min();
    std::string last_output_value;  // To detect changes
    
    // Periodic processing
    std::chrono::steady_clock::time_point last_process = std::chrono::steady_clock::time_point::min();
    bool needs_periodic_update = false;  // Set based on update_trigger
};

class SignalDAG {
public:
    SignalDAG() = default;
    ~SignalDAG() = default;
    
    // Build DAG from signal mappings
    bool build(const std::unordered_map<std::string, SignalMapping>& mappings);
    
    // Get processing order (topologically sorted)
    const std::vector<SignalNode*>& get_processing_order() const {
        return processing_order_;
    }
    
    // Get all nodes
    const std::vector<std::unique_ptr<SignalNode>>& get_nodes() const {
        return nodes_;
    }
    
    // Get node by signal name
    SignalNode* get_node(const std::string& signal_name) {
        auto it = signal_map_.find(signal_name);
        return it != signal_map_.end() ? it->second : nullptr;
    }
    
    // Get node that provides a value
    SignalNode* get_provider(const std::string& value_name) {
        auto it = provider_map_.find(value_name);
        return it != provider_map_.end() ? it->second : nullptr;
    }
    
    // Mark CAN signal as having new data
    void mark_can_signal_updated(const std::string& signal_name) {
        if (auto* node = get_node(signal_name)) {
            node->has_new_data = true;
            // Mark all dependents as potentially needing update
            propagate_update_flag(node);
        }
    }

private:
    std::vector<std::unique_ptr<SignalNode>> nodes_;
    std::unordered_map<std::string, SignalNode*> signal_map_;     // signal_name -> node
    std::unordered_map<std::string, SignalNode*> provider_map_;   // value_name -> node
    std::vector<SignalNode*> processing_order_;
    
    bool topological_sort();
    void propagate_update_flag(SignalNode* node);
};

} // namespace can_to_vss
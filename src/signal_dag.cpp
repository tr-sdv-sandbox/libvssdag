#include "vssdag/signal_dag.h"
#include <queue>
#include <algorithm>

namespace vssdag {

bool SignalDAG::build(const std::unordered_map<std::string, SignalMapping>& mappings) {
    nodes_.clear();
    signal_map_.clear();
    processing_order_.clear();
    
    // First pass: Create nodes
    for (const auto& [signal_name, mapping] : mappings) {
        auto node = std::make_unique<SignalNode>();
        node->signal_name = signal_name;
        node->mapping = mapping;
        node->depends_on = mapping.depends_on;
        
        // Determine if this is an input signal (has a source) or derived
        node->is_input_signal = mapping.source.is_input_signal();
        
        signal_map_[node->signal_name] = node.get();
        nodes_.push_back(std::move(node));
    }
    
    // Second pass: Build dependency edges
    for (auto& node : nodes_) {
        for (const auto& dep : node->depends_on) {
            auto it = signal_map_.find(dep);
            if (it == signal_map_.end()) {
                LOG(ERROR) << "Signal '" << node->signal_name 
                          << "' depends on '" << dep 
                          << "' which doesn't exist";
                return false;
            }
            
            // Add edge from dependency to dependent
            it->second->dependents.push_back(node.get());
            node->in_degree++;
        }
    }
    
    // Check for cycles and compute processing order
    if (!topological_sort()) {
        LOG(ERROR) << "Dependency cycle detected in signal DAG";
        return false;
    }
    
    LOG(INFO) << "Built signal DAG with " << nodes_.size() << " nodes";
    LOG(INFO) << "Processing order:";
    for (const auto* node : processing_order_) {
        std::string deps_str;
        if (!node->depends_on.empty()) {
            deps_str = " <- [";
            for (size_t i = 0; i < node->depends_on.size(); ++i) {
                if (i > 0) deps_str += ", ";
                deps_str += node->depends_on[i];
            }
            deps_str += "]";
        }
        LOG(INFO) << "  " << node->signal_name << deps_str;
    }
    
    return true;
}

bool SignalDAG::topological_sort() {
    processing_order_.clear();
    std::queue<SignalNode*> queue;
    
    // Make a copy of in_degrees for the sort
    std::unordered_map<SignalNode*, int> in_degrees;
    for (auto& node : nodes_) {
        in_degrees[node.get()] = node->in_degree;
        if (node->in_degree == 0) {
            queue.push(node.get());
        }
    }
    
    // Process nodes in topological order
    while (!queue.empty()) {
        auto* node = queue.front();
        queue.pop();
        processing_order_.push_back(node);
        
        // Process all dependents
        for (auto* dependent : node->dependents) {
            in_degrees[dependent]--;
            if (in_degrees[dependent] == 0) {
                queue.push(dependent);
            }
        }
    }
    
    // Check if all nodes were processed (no cycles)
    return processing_order_.size() == nodes_.size();
}

void SignalDAG::propagate_update_flag(SignalNode* node) {
    for (auto* dependent : node->dependents) {
        if (!dependent->has_new_data) {
            dependent->has_new_data = true;
            propagate_update_flag(dependent);
        }
    }
}

} // namespace vssdag
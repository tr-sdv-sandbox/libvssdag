#include "libVSSDAG/signal_dag.h"
#include <queue>
#include <algorithm>

namespace can_to_vss {

bool SignalDAG::build(const std::unordered_map<std::string, SignalMapping>& mappings) {
    nodes_.clear();
    signal_map_.clear();
    provider_map_.clear();
    processing_order_.clear();
    
    // First pass: Create nodes
    for (const auto& [signal_name, mapping] : mappings) {
        auto node = std::make_unique<SignalNode>();
        node->signal_name = signal_name;
        node->mapping = mapping;
        node->depends_on = mapping.depends_on;
        
        // Determine if this is a CAN signal or derived
        node->is_can_signal = mapping.depends_on.empty();
        
        // Set provides name (default to signal name if not specified)
        node->provides = mapping.provides.empty() ? signal_name : mapping.provides;
        
        // Check for duplicate providers
        if (provider_map_.count(node->provides)) {
            LOG(ERROR) << "Multiple signals provide '" << node->provides 
                      << "': " << provider_map_[node->provides]->signal_name 
                      << " and " << node->signal_name;
            return false;
        }
        
        signal_map_[node->signal_name] = node.get();
        provider_map_[node->provides] = node.get();
        nodes_.push_back(std::move(node));
    }
    
    // Second pass: Build dependency edges
    for (auto& node : nodes_) {
        for (const auto& dep : node->depends_on) {
            auto it = provider_map_.find(dep);
            if (it == provider_map_.end()) {
                LOG(ERROR) << "Signal '" << node->signal_name 
                          << "' depends on '" << dep 
                          << "' which no signal provides";
                return false;
            }
            
            // Add edge from provider to dependent
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
        LOG(INFO) << "  " << node->signal_name << " -> " << node->provides << deps_str;
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

} // namespace can_to_vss
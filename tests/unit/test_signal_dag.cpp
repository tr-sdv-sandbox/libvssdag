#include <gtest/gtest.h>
#include "vssdag/signal_dag.h"
#include "vssdag/mapping_types.h"

using namespace vssdag;

class SignalDAGTest : public ::testing::Test {
protected:
    SignalDAG dag;
    std::unordered_map<std::string, SignalMapping> mappings;
    
    void SetUp() override {
        mappings.clear();
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
};

// Test basic DAG construction with no dependencies
TEST_F(SignalDAGTest, BasicConstruction) {
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    mappings["Vehicle.Speed"] = speed_mapping;
    
    EXPECT_TRUE(dag.build(mappings));
    EXPECT_EQ(dag.get_nodes().size(), 1);
    
    auto* node = dag.get_node("Vehicle.Speed");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->signal_name, "Vehicle.Speed");
    EXPECT_TRUE(node->is_input_signal);
    EXPECT_EQ(node->depends_on.size(), 0);
}

// Test DAG with simple dependency
TEST_F(SignalDAGTest, SimpleDependency) {
    // Input signal
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    mappings["Vehicle.Speed"] = speed_mapping;
    
    // Derived signal
    SignalMapping accel_mapping;
    accel_mapping.depends_on.push_back("Vehicle.Speed");
    mappings["Vehicle.Acceleration"] = accel_mapping;
    
    EXPECT_TRUE(dag.build(mappings));
    EXPECT_EQ(dag.get_nodes().size(), 2);
    
    auto* speed_node = dag.get_node("Vehicle.Speed");
    auto* accel_node = dag.get_node("Vehicle.Acceleration");
    
    ASSERT_NE(speed_node, nullptr);
    ASSERT_NE(accel_node, nullptr);
    
    EXPECT_TRUE(speed_node->is_input_signal);
    EXPECT_FALSE(accel_node->is_input_signal);
    
    // Check dependencies
    EXPECT_EQ(accel_node->depends_on.size(), 1);
    EXPECT_EQ(accel_node->depends_on[0], "Vehicle.Speed");
    
    // Check dependents
    EXPECT_EQ(speed_node->dependents.size(), 1);
    EXPECT_EQ(speed_node->dependents[0], accel_node);
}

// Test multi-level dependencies
TEST_F(SignalDAGTest, MultiLevelDependencies) {
    // Level 1: Input signals
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    mappings["Vehicle.Speed"] = speed_mapping;
    
    SignalMapping throttle_mapping;
    throttle_mapping.source.type = "dbc";
    throttle_mapping.source.name = "ThrottlePosition";
    mappings["Vehicle.Throttle"] = throttle_mapping;
    
    // Level 2: Derived from inputs
    SignalMapping accel_mapping;
    accel_mapping.depends_on.push_back("Vehicle.Speed");
    mappings["Vehicle.Acceleration"] = accel_mapping;
    
    // Level 3: Derived from derived
    SignalMapping driving_mode_mapping;
    driving_mode_mapping.depends_on.push_back("Vehicle.Acceleration");
    driving_mode_mapping.depends_on.push_back("Vehicle.Throttle");
    mappings["Vehicle.DrivingMode"] = driving_mode_mapping;
    
    EXPECT_TRUE(dag.build(mappings));
    EXPECT_EQ(dag.get_nodes().size(), 4);
    
    // Check processing order (topological sort)
    const auto& order = dag.get_processing_order();
    EXPECT_EQ(order.size(), 4);
    
    // Input signals should come before derived signals
    size_t speed_idx = 0, throttle_idx = 0, accel_idx = 0, mode_idx = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i]->signal_name == "Vehicle.Speed") speed_idx = i;
        if (order[i]->signal_name == "Vehicle.Throttle") throttle_idx = i;
        if (order[i]->signal_name == "Vehicle.Acceleration") accel_idx = i;
        if (order[i]->signal_name == "Vehicle.DrivingMode") mode_idx = i;
    }
    
    EXPECT_LT(speed_idx, accel_idx);  // Speed before Acceleration
    EXPECT_LT(accel_idx, mode_idx);   // Acceleration before DrivingMode
    EXPECT_LT(throttle_idx, mode_idx); // Throttle before DrivingMode
}

// Test circular dependency detection
TEST_F(SignalDAGTest, CircularDependencyDetection) {
    SignalMapping a_mapping;
    a_mapping.depends_on.push_back("SignalB");
    mappings["SignalA"] = a_mapping;
    
    SignalMapping b_mapping;
    b_mapping.depends_on.push_back("SignalC");
    mappings["SignalB"] = b_mapping;
    
    SignalMapping c_mapping;
    c_mapping.depends_on.push_back("SignalA");  // Creates cycle: A -> B -> C -> A
    mappings["SignalC"] = c_mapping;
    
    EXPECT_FALSE(dag.build(mappings));  // Should fail due to circular dependency
}

// Test self-dependency detection
TEST_F(SignalDAGTest, SelfDependencyDetection) {
    SignalMapping self_mapping;
    self_mapping.depends_on.push_back("SignalA");  // Depends on itself
    mappings["SignalA"] = self_mapping;
    
    EXPECT_FALSE(dag.build(mappings));  // Should fail due to self-dependency
}

// Test update propagation
TEST_F(SignalDAGTest, UpdatePropagation) {
    // Create a simple chain: A -> B -> C
    SignalMapping a_mapping;
    a_mapping.source.type = "dbc";
    a_mapping.source.name = "SignalA";
    mappings["A"] = a_mapping;
    
    SignalMapping b_mapping;
    b_mapping.depends_on.push_back("A");
    mappings["B"] = b_mapping;
    
    SignalMapping c_mapping;
    c_mapping.depends_on.push_back("B");
    mappings["C"] = c_mapping;
    
    EXPECT_TRUE(dag.build(mappings));
    
    // Mark A as updated
    dag.mark_can_signal_updated("A");
    
    auto* a_node = dag.get_node("A");
    auto* b_node = dag.get_node("B");
    auto* c_node = dag.get_node("C");
    
    // All nodes should have new data flag set
    EXPECT_TRUE(a_node->has_new_data);
    EXPECT_TRUE(b_node->has_new_data);
    EXPECT_TRUE(c_node->has_new_data);
}

// Test missing dependency handling
TEST_F(SignalDAGTest, MissingDependency) {
    SignalMapping mapping;
    mapping.depends_on.push_back("NonExistentSignal");
    mappings["DerivedSignal"] = mapping;
    
    EXPECT_FALSE(dag.build(mappings));  // Should fail due to missing dependency
}

// Test complex DAG with multiple paths
TEST_F(SignalDAGTest, ComplexDAGMultiplePaths) {
    // Create a diamond pattern:
    //     A
    //    / \
    //   B   C
    //    \ /
    //     D
    
    SignalMapping a_mapping;
    a_mapping.source.type = "dbc";
    a_mapping.source.name = "SourceA";
    mappings["A"] = a_mapping;
    
    SignalMapping b_mapping;
    b_mapping.depends_on.push_back("A");
    mappings["B"] = b_mapping;
    
    SignalMapping c_mapping;
    c_mapping.depends_on.push_back("A");
    mappings["C"] = c_mapping;
    
    SignalMapping d_mapping;
    d_mapping.depends_on.push_back("B");
    d_mapping.depends_on.push_back("C");
    mappings["D"] = d_mapping;
    
    EXPECT_TRUE(dag.build(mappings));
    EXPECT_EQ(dag.get_nodes().size(), 4);
    
    // Verify processing order
    const auto& order = dag.get_processing_order();
    size_t a_idx = 0, b_idx = 0, c_idx = 0, d_idx = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i]->signal_name == "A") a_idx = i;
        if (order[i]->signal_name == "B") b_idx = i;
        if (order[i]->signal_name == "C") c_idx = i;
        if (order[i]->signal_name == "D") d_idx = i;
    }
    
    EXPECT_LT(a_idx, b_idx);
    EXPECT_LT(a_idx, c_idx);
    EXPECT_LT(b_idx, d_idx);
    EXPECT_LT(c_idx, d_idx);
}
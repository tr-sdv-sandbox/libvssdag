# libVSSDAG

A high-performance C++ library for transforming automotive CAN bus signals into [Vehicle Signal Specification (VSS)](https://covesa.github.io/vehicle_signal_specification/) format using a Directed Acyclic Graph (DAG) architecture.

Inspired by https://github.com/eclipse-kuksa/kuksa-can-provider

## Overview

libVSSDAG provides a flexible signal processing pipeline that transforms raw CAN messages into standardized VSS signals. The library uses a DAG-based approach to handle complex signal dependencies, enabling sophisticated transformations and derived signal calculations through embedded Lua scripting.

### Key Features

- **DAG-based Signal Processing**: Automatically resolves signal dependencies and processes in optimal order
- **Lua Transformations**: Embed complex logic, state machines, and calculations directly in YAML configurations
- **VSS 4.0 Support**: Native support for struct types and nested data structures
- **Real-time Processing**: Optimized for automotive real-time requirements
- **Flexible Data Sources**: Support for live CAN interfaces and log replay
- **Signal Aggregation**: Multiple strategies for handling slowly-arriving distributed data

## Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install -y \
    build-essential \
    cmake \
    libgoogle-glog-dev \
    liblua5.3-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    can-utils

# Or use the provided script
./install_prereq.sh
```

### Building

```bash
# Quick build (from root directory)
./build.sh

# Or manually
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON
make -j$(nproc)
```

### Library Integration

```cmake
# In your CMakeLists.txt
find_package(libVSSDAG REQUIRED)
target_link_libraries(your_target PRIVATE VSSDAG)
```

## Library Usage

### Basic Example

```cpp
#include <vssdag/signal_processor.h>
#include <vssdag/can/can_source.h>

using namespace vssdag;

// Initialize the processor
SignalProcessorDAG processor;
processor.initialize(mappings);

// Create a CAN signal source
CANSignalSource can_source("can0", "vehicle.dbc");

// Process updates
while (running) {
    auto updates = can_source.get_updates();
    auto vss_signals = processor.process_signal_updates(updates);
    
    for (const auto& signal : vss_signals) {
        // Handle VSS signal
        std::cout << signal.path << " = " << signal.value << std::endl;
    }
}
```

### Configuration Format

Signal mappings are defined in YAML:

```yaml
mappings:
  # Simple CAN signal mapping
  - signal: Vehicle.Speed
    source:
      type: dbc
      name: DI_vehicleSpeed
    datatype: float
    transform:
      code: "lowpass(x, 0.3)"  # Lua transform with low-pass filter
    
  # Derived signal with dependencies
  - signal: Vehicle.Acceleration
    depends_on: [Vehicle.Speed]
    datatype: float
    transform:
      code: |
        local speed = dependencies["Vehicle.Speed"]
        return derivative(speed, "Vehicle.Speed")
    
  # Struct type for grouped data
  - signal: Vehicle.Powertrain.Battery.Status
    depends_on: [Battery.Voltage, Battery.Current, Battery.Temperature]
    datatype: struct
    struct_type: BatteryStatus
    transform:
      code: |
        return {
          voltage = dependencies["Battery.Voltage"],
          current = dependencies["Battery.Current"],
          temperature = dependencies["Battery.Temperature"],
          power = dependencies["Battery.Voltage"] * dependencies["Battery.Current"]
        }
```

## Architecture

### Signal Processing Pipeline

```
CAN Bus → DBC Parser → Signal Source → DAG Processor → VSS Output
                            ↓              ↓
                     Signal Updates   Lua Transforms
                                          ↓
                                   Derived Signals
```

### Core Components

- **SignalProcessorDAG**: Main processing engine with dependency resolution
- **SignalDAG**: Manages signal topology and processing order
- **LuaMapper**: Executes Lua transformations with stateful context
- **CANSignalSource**: Interfaces with CAN bus via SocketCAN
- **DBCParser**: Parses DBC files for signal definitions
- **VSSFormatter**: Formats output according to VSS specifications

## Examples

The repository includes comprehensive examples demonstrating various use cases:

### Running Examples

```bash
# Build the library and examples
./build.sh

# Run the CAN transformer example
./build/examples/can_transformer/can-transformer <dbc_file> <mapping_yaml> <can_interface>

# Tesla Model 3 CAN processing
cd examples/tesla_model3
./run_demo.sh

# Battery management system simulation  
cd examples/battery_management
./run_demo.sh
```

### Example Applications

- **can-transformer**: Reference implementation showing library usage
- **Tesla Simulation**: Real-world CAN data processing with derived signals
- **Battery Simulation**: Demonstrates aggregation strategies for distributed sensors

## Advanced Features

### Lua Transform Functions

The library provides built-in Lua functions for common operations:

- `lowpass(value, alpha)`: Low-pass filter
- `derivative(value, signal_name)`: Calculate rate of change
- `moving_average(value, signal_name, window)`: Moving average
- `threshold(value, limit)`: Threshold detection
- `state_machine(state, event)`: State machine implementation

### Signal Dependencies

Signals can depend on multiple inputs:

```yaml
- signal: Vehicle.Powertrain.Efficiency
  depends_on: 
    - Vehicle.Powertrain.Motor.Power
    - Vehicle.Powertrain.Battery.Power
  transform:
    code: |
      local motor = dependencies["Vehicle.Powertrain.Motor.Power"]
      local battery = dependencies["Vehicle.Powertrain.Battery.Power"]
      return (motor / battery) * 100
```

### Update Triggers

Control when signals are processed:

- `change`: Process when input changes
- `periodic`: Process at regular intervals
- `both`: Combine change and periodic triggers

## Testing

```bash
# Run tests (when available)
cd build
ctest
```

## Contributing

Contributions are welcome! Please ensure:

1. Code follows C++17 standards
2. New features include tests
3. Documentation is updated
4. Examples demonstrate new capabilities

## License

[Specify your license here]

## Roadmap
- [ ] VSS path validation against .vspec schemas
- [ ] Additional signal source types (Ethernet)
- [ ] Performance optimizations for high-frequency signals

## Support

For issues, questions, or contributions, please [open an issue](https://github.com/yourusername/libVSSDAG/issues).

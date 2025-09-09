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
- **Invalid/Not-Available Signal Handling**: Automatic detection and propagation of invalid sensor states

## Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install -y \
    build-essential \
    cmake \
    libgoogle-glog-dev \
    liblua5.4-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libxml2-dev \
    can-utils

# For testing (optional)
sudo apt-get install -y \
    libgtest-dev \
    libgmock-dev

# Or use the provided script
./install_prereq.sh
```

Note: The build system will automatically fetch missing dependencies:
- yaml-cpp 0.8.0 (if not found)
- nlohmann/json 3.11.2 (if not found)
- dbcppp (CAN DBC parser)
- moodycamel/concurrentqueue 1.0.4

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
add_subdirectory(path/to/libVSSDAG)
target_link_libraries(your_target PRIVATE vssdag)
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
        // Handle VSS signal with status
        if (signal.status == SignalStatus::Valid) {
            std::cout << signal.path << " = " << signal.value << std::endl;
        } else if (signal.status == SignalStatus::Invalid) {
            std::cout << signal.path << " = INVALID" << std::endl;
        } else if (signal.status == SignalStatus::NotAvailable) {
            std::cout << signal.path << " = NOT AVAILABLE" << std::endl;
        }
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
./run_can_replay.sh   # For replaying logged CAN data
# or
./run_transformer.sh  # For live CAN interface

# Battery management system simulation  
cd examples/battery_management
./run_battery_simulation.sh
```

### Example Applications

- **can-transformer**: Reference implementation showing library usage
- **Tesla Simulation**: Real-world CAN data processing with derived signals
- **Battery Simulation**: Demonstrates aggregation strategies for distributed sensors
- **Invalid Signal Handling**: Examples of detecting and handling sensor failures (see `examples/invalid_signal_handling.yaml`)

## Advanced Features

### Lua Transform Functions

The library provides built-in Lua functions for common operations:

- `lowpass(value, alpha)`: Low-pass filter with configurable invalid signal strategies
- `derivative(value, signal_name)`: Calculate rate of change
- `moving_average(value, signal_name, window)`: Moving average
- `threshold(value, limit)`: Threshold detection
- `state_machine(state, event)`: State machine implementation

### Invalid/Not-Available Signal Handling

The library automatically detects and propagates invalid and not-available signals from CAN bus:

#### Automatic Detection
- **Invalid signals**: Detected when all bits are set (0xFF pattern) or values are out of range
- **Not-Available signals**: Detected when all bits minus one are set (0xFE pattern)
- **DBC-aware**: Respects signal ranges defined in DBC files

#### Lua Signal Status
Signals in Lua have associated status information:

```lua
-- Invalid/NA signals are represented as nil
if dependencies["Battery.Voltage"] == nil then
    -- Check status table for reason
    if status["Battery.Voltage"] == STATUS_INVALID then
        -- Sensor failure or out-of-range
    elseif status["Battery.Voltage"] == STATUS_NOT_AVAILABLE then
        -- Sensor not equipped or not ready
    end
end

-- Built-in filter strategies for invalid signals
lowpass(value, alpha, STRATEGY_HOLD)  -- Hold last valid value
lowpass(value, alpha, STRATEGY_PROPAGATE)  -- Propagate nil
lowpass(value, alpha, STRATEGY_HOLD_TIMEOUT, 5000)  -- Hold with timeout
```

#### Status Constants
```lua
-- Signal status values
STATUS_VALID = 0
STATUS_INVALID = 1
STATUS_NOT_AVAILABLE = 2

-- Filter strategies
STRATEGY_PROPAGATE = 0
STRATEGY_HOLD = 1
STRATEGY_HOLD_TIMEOUT = 2
```

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

For issues, questions, or contributions, please [open an issue](https://github.com/skarlsson/libVSSDAG/issues).

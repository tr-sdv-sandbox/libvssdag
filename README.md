# libVSSDAG

C++ library for transforming CAN bus signals into VSS format via DAG-based processing pipeline with embedded Lua transforms.

Inspired by [eclipse-kuksa/kuksa-can-provider](https://github.com/eclipse-kuksa/kuksa-can-provider)

## Overview

Processes raw CAN messages through a dependency-aware pipeline: DBC parsing → topological sort → Lua transforms → VSS output. Handles multi-level derived signals, VSS 4.0 structs, and automatic invalid/not-available signal propagation.

**Core capabilities:**
- DAG topology automatically resolves signal dependencies and ensures correct processing order
- Lua scripting engine for stateful transforms (filters, derivatives, state machines)
- VSS 4.0 struct aggregation with built-in quality tracking (VALID/INVALID/NOT_AVAILABLE)
- Lock-free queuing for real-time automotive constraints
- Extensible ISignalSource interface for CAN, SOME/IP, MQTT, etc.

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

## API

### Core Types

```cpp
// Signal update from sources (include/vssdag/signal_source.h:19)
struct SignalUpdate {
    std::string signal_name;
    vss::types::Value value;  // Typed VSS value
    std::chrono::steady_clock::time_point timestamp;
    vss::types::SignalQuality status;  // VALID/INVALID/NOT_AVAILABLE
};

// Main processor interface (include/vssdag/signal_processor.h:14)
class SignalProcessorDAG {
    bool initialize(const std::unordered_map<std::string, SignalMapping>& mappings);
    std::vector<VSSSignal> process_signal_updates(const std::vector<SignalUpdate>& updates);
    std::vector<std::string> get_required_input_signals() const;
};

// Signal source interface (include/vssdag/signal_source.h:26)
class ISignalSource {
    virtual bool initialize() = 0;
    virtual std::vector<SignalUpdate> poll() = 0;  // Non-blocking
    virtual std::vector<std::string> get_exported_signals() const = 0;
};

// Transform types (include/vssdag/mapping_types.h:12)
struct DirectMapping {};
struct CodeTransform { std::string expression; };  // Lua code
struct ValueMapping { std::unordered_map<std::string, std::string> mappings; };
using Transform = std::variant<DirectMapping, CodeTransform, ValueMapping>;

// Signal mapping configuration (include/vssdag/mapping_types.h:33)
struct SignalMapping {
    ValueType datatype;
    Transform transform;
    std::vector<std::string> depends_on;  // Dependencies for DAG
    UpdateTrigger update_trigger;  // ON_DEPENDENCY | PERIODIC | BOTH
    int interval_ms;  // Throttling/periodic interval
    SignalSource source;  // Source info for input signals
    std::string struct_type;  // VSS 4.0 struct type
    bool is_struct;
};
```

### Basic Usage

```cpp
#include <vssdag/signal_processor.h>
#include <vssdag/can/can_source.h>

// Parse YAML mappings
auto mappings = parse_yaml("mappings.yaml");

// Initialize processor with DAG
SignalProcessorDAG processor;
processor.initialize(mappings);

// Create CAN source
auto can_source = std::make_unique<CANSignalSource>("can0", "vehicle.dbc", mappings);
can_source->initialize();

// Main processing loop
while (running) {
    auto updates = can_source->poll();  // Non-blocking
    auto vss_signals = processor.process_signal_updates(updates);

    for (const auto& signal : vss_signals) {
        if (signal.qualified_value.quality == SignalQuality::VALID) {
            std::cout << signal.path << " = " << signal.qualified_value.value << std::endl;
        }
    }
}
```

### YAML Configuration

```yaml
# Direct CAN mapping with Lua filter
- signal: Vehicle.Speed
  source: {type: dbc, name: DI_vehicleSpeed}
  datatype: float
  transform:
    code: "lowpass(x, 0.3)"

# Derived signal (dependencies trigger processing)
- signal: Vehicle.Acceleration.Longitudinal
  depends_on: [Vehicle.Speed]
  datatype: float
  transform:
    code: "return derivative(deps['Vehicle.Speed']) * 0.277778"

# Multi-dependency with conditional logic
- signal: Telemetry.HarshBraking
  depends_on: [Vehicle.Acceleration.Longitudinal, Vehicle.Speed]
  datatype: boolean
  transform:
    code: |
      local accel = deps['Vehicle.Acceleration.Longitudinal']
      return accel < -19.6 and sustained_condition(true, 200)

# VSS 4.0 struct aggregation
- signal: Vehicle.DynamicsStruct
  datatype: struct
  struct_type: Types.VehicleDynamics
  depends_on: [Vehicle.Speed, Vehicle.Acceleration.Longitudinal]
  transform:
    code: |
      return {
        Speed = deps['Vehicle.Speed'],
        LongitudinalAcceleration = deps['Vehicle.Acceleration.Longitudinal']
      }

# Periodic trigger (runs every 100ms regardless of dependencies)
- signal: Vehicle.Powertrain.Efficiency
  depends_on: [Vehicle.Powertrain.Motor.Power, Vehicle.Powertrain.Battery.Power]
  update_trigger: periodic
  interval_ms: 100
  transform:
    code: "return (deps['Vehicle.Powertrain.Motor.Power'] / deps['Vehicle.Powertrain.Battery.Power']) * 100"

# Delayed propagation (actuator simulation - door lock takes 200ms to engage)
# Note: delayed() automatically triggers periodic re-evaluation until delay elapses
- signal: Vehicle.Cabin.Door.Row1.Left.IsLocked
  depends_on: [Vehicle.Cabin.Door.Row1.Left.IsLocked.Target]
  datatype: boolean
  transform:
    code: "delayed(deps['Vehicle.Cabin.Door.Row1.Left.IsLocked.Target'], 200)"
```

## Architecture

**Pipeline:** CAN frames → DBC decode → SignalUpdate → DAG topological sort → Lua transforms → VSSSignal output

**Key components:**
- `SignalProcessorDAG`: Orchestrates DAG initialization and signal processing
- `SignalDAG`: Builds dependency graph, performs topological sort
- `LuaMapper`: Executes transforms with stateful context (filters maintain history)
- `CANSignalSource`: SocketCAN reader + DBC parser, detects invalid/not-available signals
- `DBCParser`: Decodes frames using libdbcppp, validates ranges

**Processing model:**
1. Signal updates trigger topological traversal from dependency roots
2. Each node processes when dependencies update or periodic timer fires
3. Lua context receives `deps` table with dependency values and `status` table for quality
4. Invalid/not-available signals propagate as `nil` in Lua with status metadata
5. Filters (lowpass, derivative) use configurable strategies: PROPAGATE, HOLD, or HOLD_TIMEOUT

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

## Lua API

### Built-in Functions

```lua
-- Filters
lowpass(x, alpha)                           -- Exponential moving average
lowpass(x, alpha, STRATEGY_HOLD)            -- Hold last valid on invalid input
lowpass(x, alpha, STRATEGY_HOLD_TIMEOUT, 5000)  -- Hold with 5s timeout
moving_average(x, signal_name, window)      -- Sliding window average
derivative(x, signal_name)                  -- Rate of change
threshold(x, limit)                         -- Boolean threshold
sustained_condition(condition, duration_ms) -- Debounce/sustain logic
state_machine(state, event)                 -- State machine transitions

-- Timing
delayed(value, delay_ms)                    -- Delay value propagation by specified milliseconds
                                            -- Returns nil until delay elapses after value change
                                            -- Useful for actuator simulation (e.g., door locks take 200ms)
```

### Context Variables

```lua
-- Dependency access
deps['Vehicle.Speed']           -- Returns value or nil if invalid/not-available

-- Status checking
status['Vehicle.Speed']         -- STATUS_VALID | STATUS_INVALID | STATUS_NOT_AVAILABLE

-- Constants
STATUS_VALID = 0
STATUS_INVALID = 1              -- Sensor failure or out of range
STATUS_NOT_AVAILABLE = 2        -- Sensor not equipped or not ready

STRATEGY_PROPAGATE = 0          -- Pass nil through filters
STRATEGY_HOLD = 1               -- Maintain last valid value
STRATEGY_HOLD_TIMEOUT = 2       -- Hold with timeout then propagate
```

### Invalid Signal Handling

Invalid signals detected via DBC range checks or bit patterns (0xFF=INVALID, 0xFE=NOT_AVAILABLE):

```lua
-- Check and handle invalid signals
if deps['Battery.Voltage'] == nil then
    if status['Battery.Voltage'] == STATUS_INVALID then
        return 0  -- Provide fallback
    elseif status['Battery.Voltage'] == STATUS_NOT_AVAILABLE then
        return nil  -- Propagate unavailability
    end
end

-- Use filter strategies
local filtered = lowpass(deps['Battery.Voltage'], 0.3, STRATEGY_HOLD_TIMEOUT, 5000)
```

## Testing

```bash
cd build && ctest
# or: ./build/tests/unit/signal_processor_test
```

Tests cover: DAG initialization, topological sort, derived signals, structs, invalid signal propagation, filter strategies, periodic triggers.

## Dependencies

- **glog**: Logging framework
- **lua5.4/5.3**: Scripting engine for transforms
- **yaml-cpp**: YAML parsing
- **nlohmann_json**: JSON utilities
- **dbcppp**: DBC file parser
- **moodycamel::concurrentqueue**: Lock-free queuing
- **libvss-types**: VSS type definitions

## License

Apache License 2.0




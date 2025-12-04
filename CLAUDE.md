# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

libVSSDAG is a C++17 library for transforming CAN bus signals into VSS (Vehicle Signal Specification) format through a DAG-based processing pipeline with embedded Lua transforms. It processes raw CAN messages through: DBC parsing → topological sort → Lua transforms → VSS output.

## Build Commands

```bash
# Quick build (library + examples + tests)
./build.sh

# Manual build
mkdir -p build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
make -j$(nproc)

# Build options: BUILD_EXAMPLES, BUILD_TESTS, BUILD_INTEGRATION_TESTS (all default ON), BUILD_SHARED_LIBS (default OFF)

# Clean rebuild
./rebuild.sh
```

## Virtual CAN Setup (for testing)

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

## Testing

```bash
cd build && ctest --output-on-failure

# Individual tests
./build/tests/unit/test_signal_processor
./build/tests/unit/test_lua_mapper
./build/tests/unit/test_signal_dag
./build/tests/integration/test_can_replay
./build/tests/integration/test_end_to_end
```

## Running Examples

```bash
# CAN transformer
./build/examples/can_transformer/can-transformer <dbc_file> <mapping_yaml> <can_interface>

# Tesla Model 3 simulation
cd examples/tesla_model3 && ./run_can_replay.sh

# Battery simulation
cd examples/battery_management && ./run_battery_simulation.sh
```

## Architecture

**Processing Pipeline:** CAN frames → SocketCANReader → DBCParser → SignalUpdate → SignalDAG → LuaMapper → VSSSignal output

**Key Components:**
- `SignalProcessorDAG` (include/vssdag/signal_processor.h) - Main orchestrator, initializes DAG from mappings, processes signal updates
- `SignalDAG` (include/vssdag/signal_dag.h) - Dependency graph with topological sort for correct processing order
- `LuaMapper` (include/vssdag/lua_mapper.h) - Lua state management, executes transforms with `deps[]` and `status[]` context
- `CANSignalSource` (include/vssdag/can/can_source.h) - ISignalSource implementation for CAN bus, uses lock-free queue
- `DBCParser` (include/vssdag/can/dbc_parser.h) - DBC file parsing via dbcppp library

**Processing Model:**
1. Signal updates trigger topological traversal from dependency roots
2. Each node processes when dependencies update or periodic timer fires
3. Lua receives `deps` table (dependency values) and `status` table (quality: VALID/INVALID/NOT_AVAILABLE)
4. Invalid signals propagate as `nil` in Lua with status metadata
5. Filters use strategies: PROPAGATE, HOLD, or HOLD_TIMEOUT

## Code Conventions

- Namespace: `vssdag::`
- Methods: snake_case (`initialize()`, `process_signal_updates()`)
- Members: trailing underscore (`nodes_`, `lua_mapper_`)
- Constants: UPPER_CASE (`STATUS_VALID`, `STRATEGY_HOLD`)
- Header guards: `#pragma once`
- Logging: glog (`LOG(INFO)`, `LOG(ERROR)`)
- Smart pointers throughout; raw pointers only for DAG traversal within scope
- Lock-free design using moodycamel::ConcurrentQueue for real-time constraints

## Lua Transform API

Built-in functions available in transforms:
- `lowpass(x, alpha, [strategy], [timeout_ms])` - Exponential moving average
- `moving_average(x, signal_name, window)` - Sliding window
- `derivative(x, signal_name)` - Rate of change
- `threshold(x, limit)` - Boolean threshold
- `sustained_condition(condition, duration_ms)` - Debounce
- `delayed(value, delay_ms)` - Delay value propagation
- `state_machine(state, event)` - State transitions

Context: `deps['Signal.Name']` returns value or nil; `status['Signal.Name']` returns STATUS_VALID/STATUS_INVALID/STATUS_NOT_AVAILABLE

## Signal Mapping Configuration

Transform types in YAML mappings (see `include/vssdag/mapping_types.h`):
- **DirectMapping** - Pass-through from source signal
- **CodeTransform** - Lua expression (`transform: { code: "lowpass(x, 0.3)" }`)
- **ValueMapping** - Discrete value lookup table

UpdateTrigger options: `ON_DEPENDENCY` (default), `PERIODIC`, `BOTH`

```yaml
- signal: Vehicle.Speed
  source: {type: dbc, name: DI_vehicleSpeed}
  datatype: float
  transform:
    code: "lowpass(x, 0.3)"

- signal: Vehicle.Acceleration
  depends_on: [Vehicle.Speed]
  datatype: float
  transform:
    code: "return derivative(deps['Vehicle.Speed'])"
```

## Dependencies

glog, lua5.4/5.3, yaml-cpp, nlohmann_json, dbcppp, moodycamel::concurrentqueue, libvss-types

For libvss-types: either install system-wide or place in parent directory (`../libvss-types`). See BUILD.md for full dependency installation instructions.

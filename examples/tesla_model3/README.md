# Tesla Model 3 CAN to VSS Simulation

This example demonstrates processing real Tesla Model 3 CAN data and mapping it to VSS signals using the DAG-based signal processor.

## Files

- `Model3CAN.dbc` - Tesla Model 3 CAN database file
- `model3_mappings_dag.yaml` - Signal mappings with DAG dependencies and Lua transforms
- `candump.log` - Sample CAN log data from a real Tesla Model 3
- `run_can_to_vss_dag.sh` - Script to run the CAN to VSS converter
- `run_can_replay.sh` - Script to replay CAN log data on virtual CAN interface

## Features Demonstrated

1. **DAG-based Signal Processing**
   - Multi-signal dependencies
   - Derived signals from multiple CAN signals
   - Stateful transforms with history tracking

2. **Advanced Lua Transforms**
   - Low-pass filtering for noisy signals
   - State machines for gear detection
   - Complex calculations (e.g., brake power, efficiency)
   - Pattern detection (harsh braking, emergency stops)

3. **VSS Struct Support**
   - Vehicle dynamics struct combining speed and acceleration
   - Power train struct with motor and battery data

## Running the Simulation

### Prerequisites

1. Build the project:
```bash
cd ../..
./build.sh
```

2. Set up virtual CAN interface:
```bash
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### Run the Demo

Terminal 1 - Start the CAN to VSS converter:
```bash
cd ../..
./examples/tesla_simulation/run_can_to_vss_dag.sh
```

Terminal 2 - Replay Tesla CAN data:
```bash
cd ../..
./examples/tesla_simulation/run_can_replay.sh
```

### Custom Usage

Process with different files:
```bash
./build/can-to-vss-dag <dbc_file> <mapping_file> <can_interface>
```

Replay at different speeds:
```bash
./examples/tesla_simulation/run_can_replay.sh vcan0 candump.log 2  # 2x speed
```

## Signal Examples

### Basic Signals
- Vehicle speed with low-pass filtering
- Brake pedal state
- Throttle position
- Steering wheel angle

### Derived Signals  
- Harsh braking detection (deceleration > 6 m/s²)
- Emergency stop detection (harsh brake + ABS active)
- Steering aggressiveness (angular velocity threshold)
- Driving mode inference

### Struct Signals
- Vehicle dynamics (speed + acceleration)
- Powertrain status (gear, motor state, efficiency)

## Customization

Edit `model3_mappings_dag.yaml` to:
- Add new signal mappings
- Modify transform functions
- Change dependencies
- Adjust thresholds and parameters

## Troubleshooting

1. **No CAN data received**: Check virtual CAN interface is up
2. **Transform errors**: Check Lua syntax in mapping file
3. **Missing dependencies**: Ensure all dependent signals are defined
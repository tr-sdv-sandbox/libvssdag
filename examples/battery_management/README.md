# Battery Cell Aggregation Simulation

This example demonstrates different strategies for handling slowly arriving battery cell data in an EV battery management system.

## Problem

Battery Management Systems (BMS) often scan cells sequentially:
- 12-108 cells in typical EV battery packs
- Each cell has voltage, temperature, and other parameters
- Full scan cycle takes 15-30 seconds
- Data arrives asynchronously, one or few cells at a time

## Files

- `BatterySimulation.dbc` - CAN database defining battery cell messages
- `battery_cells_mapping.yaml` - VSS mapping with three aggregation strategies
- `battery_types.vspec` - VSS struct type definitions for battery data
- `simulate_battery_cells.sh` - Simulates BMS sending cell data on CAN bus
- `run_battery_simulation.sh` - Runs the complete demo

## Aggregation Strategies

### 1. Last Known Value (LKV)
- **Output**: `Vehicle.Powertrain.TractionBattery.CellsLKV`
- **Behavior**: Always maintains complete data, updates individual cells as they arrive
- **Pros**: Always have complete data, simple logic
- **Cons**: Can't distinguish fresh from stale data
- **Use for**: Control systems, continuous monitoring

### 2. Windowed with Freshness
- **Output**: `Vehicle.Powertrain.TractionBattery.CellsWindowed`
- **Behavior**: Only includes cells updated within time window (e.g., 10 seconds)
- **Pros**: Know which data is fresh
- **Cons**: May have incomplete data
- **Use for**: Diagnostics, anomaly detection

### 3. Complete Cycle Detection
- **Output**: `Vehicle.Powertrain.TractionBattery.CellsComplete`
- **Behavior**: Only emits when all cells have been recently updated
- **Pros**: Guarantees data consistency
- **Cons**: Long delays between updates, may never emit if cells missing
- **Use for**: SOH calculations, critical decisions

## Running the Demo

1. **Basic run** - Starts simulator and processor together:
```bash
./run_battery_simulation.sh
```

2. **Manual control** - Run components separately:

Terminal 1 - Start CAN to VSS processor:
```bash
cd ../..
./build/can-to-vss-dag \
    examples/battery_simulation/BatterySimulation.dbc \
    examples/battery_simulation/battery_cells_mapping.yaml \
    vcan0
```

Terminal 2 - Run simulator with different scenarios:
```bash
./simulate_battery_cells.sh normal   # Sequential scanning
./simulate_battery_cells.sh fast     # Rapid scanning
./simulate_battery_cells.sh missing  # Simulates fault (group 2 missing)
./simulate_battery_cells.sh random   # Random arrival order
./simulate_battery_cells.sh burst    # Burst mode with long pauses
```

## CAN Message Format

### Cell Voltage Messages (0x252, 0x262, 0x272, 0x282)
```
Bytes 0-1: Cell 1 voltage (mV, big-endian)
Bytes 2-3: Cell 2 voltage (mV, big-endian)  
Bytes 4-5: Cell 3 voltage (mV, big-endian)
Byte 6: Group ID (0-3)
Byte 7: Update counter
```

### Statistics Message (0x332)
```
Bytes 0-1: Min cell voltage (mV)
Bytes 2-3: Max cell voltage (mV)
Byte 4: Min cell ID
Byte 5: Max cell ID
Bytes 6-7: Delta voltage (mV)
```

## Expected Output

When running, you'll see three different struct outputs:

```
VSS: Vehicle.Powertrain.TractionBattery.CellsLKV = {...}      # Updates frequently
VSS: Vehicle.Powertrain.TractionBattery.CellsWindowed = {...}  # Every 5 seconds
VSS: Vehicle.Powertrain.TractionBattery.CellsComplete = {...}  # Only when complete
```

The LKV strategy will update with every new cell value, Windowed will show periodic snapshots with freshness info, and Complete will only emit after all 12 cells have been scanned.

## Customization

### Adjust timing in `battery_cells_mapping.yaml`:

- `interval_ms`: How often to emit struct
- `window_seconds`: Freshness window for windowed strategy
- `update_trigger`: `change`, `periodic`, or `both`

### Add more cells:

1. Extend DBC file with more cell messages
2. Add cell signal mappings in YAML
3. Update derived signal dependencies
4. Modify simulator to send more groups

### Production considerations:

- Use actual cell count (96-108 for Tesla)
- Implement compression for large cell arrays
- Add fault detection for missing cells
- Include cell balancing status
- Track State of Health metrics
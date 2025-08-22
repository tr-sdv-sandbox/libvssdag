# VSS 4.0 Struct Implementation Notes

## Current Implementation Status

Our implementation supports VSS 4.0 struct types in the signal processing pipeline, but the .vspec files are currently for documentation only. They are not parsed or validated at runtime.

## VSS 4.0 Struct Format

According to VSS 4.0 specification, structs can be defined in two ways:

### 1. Named Type Definition (Recommended)
```yaml
# Define reusable struct type
Vehicle.Types.BatteryStatus:
  type: struct
  description: Battery status information

Vehicle.Types.BatteryStatus.voltage:
  datatype: float
  type: property
  unit: V

Vehicle.Types.BatteryStatus.current:
  datatype: float
  type: property
  unit: A

# Use the defined type
Vehicle.Powertrain.TractionBattery.Status:
  datatype: Vehicle.Types.BatteryStatus
  type: sensor
```

### 2. Inline Struct Definition
```yaml
Vehicle.Powertrain.TractionBattery.Status:
  type: sensor
  datatype:
    type: struct
    properties:
      voltage:
        datatype: float
        unit: V
      current:
        datatype: float
        unit: A
```

## Current Files

- `data/vss_types.vspec` - Tesla simulation struct types (documentation only)
- `examples/battery_simulation/battery_types.vspec` - Battery struct types (documentation only)
- `examples/battery_simulation/battery_types_vss4.vspec` - VSS 4.0 compliant example

## Future Integration with Kuksa.val

When integrating with kuksa.val databroker, we will need to:

1. **Parse .vspec files** to understand the expected struct schema
2. **Validate struct outputs** from Lua transforms against the schema
3. **Generate proper VSS paths** for struct fields
4. **Handle struct serialization** for the databroker protocol (likely Protobuf)

## Implementation in Code

Currently, structs are created in Lua transforms by returning tables:

```lua
return {
  voltage = 3.7,
  current = 150.5,
  temperature = 25.3
}
```

The C++ code (vss_types.cpp) converts these Lua tables to JSON for output. The `struct_type` field in YAML mappings is metadata that could be used for validation but isn't currently enforced.

## Recommendations

1. **Keep .vspec files** for future kuksa.val integration
2. **Follow VSS 4.0 format** when defining new struct types
3. **Use Vehicle.Types namespace** for reusable struct definitions
4. **Consider implementing vspec parser** when kuksa.val integration is needed
# Testing Strategy for libVSSDAG

## Overview
This document outlines the testing strategy for libVSSDAG, including unit tests and integration tests.

## Unit Testing Priority

### 1. **Core Data Types (HIGH PRIORITY)**
Start with: `test_vss_types.cpp`
- **Why first**: These are the foundation of all data handling
- **What to test**:
  - Type conversions (int → float, double → bool, etc.)
  - JSON serialization/deserialization
  - Array and struct handling
  - Edge cases (overflow, underflow, null values)

### 2. **Signal DAG (HIGH PRIORITY)**
Start with: `test_signal_dag.cpp`
- **Why early**: Core algorithm for dependency resolution
- **What to test**:
  - Topological sorting
  - Circular dependency detection
  - Update propagation
  - Complex dependency graphs
  - Edge cases (self-dependencies, missing dependencies)

### 3. **Lua Mapper (MEDIUM PRIORITY)**
Start with: `test_lua_mapper.cpp`
- **Why important**: All transformations go through this
- **What to test**:
  - Basic expressions
  - Mathematical operations
  - Conditional logic
  - State management
  - Built-in functions (lowpass, derivative, moving_average)
  - Error handling for invalid Lua code
  - Memory leaks in Lua state

### 4. **DBC Parser (MEDIUM PRIORITY)**
Start with: `test_dbc_parser.cpp`
- **Why**: Input validation and CAN signal decoding
- **What to test**:
  - Valid DBC file parsing
  - Signal extraction
  - Message decoding
  - Enum/value table handling
  - Endianness (big/little)
  - Signed/unsigned values
  - Scaling and offset

### 5. **Signal Processor (LOW PRIORITY - but important for integration)**
Start with: `test_signal_processor.cpp`
- **Why last**: Depends on all other components working
- **What to test**:
  - End-to-end signal processing
  - Partial updates
  - Struct generation
  - Performance under load

## Integration Testing

### 1. **CAN Log Replay Test**
File: `test_can_replay.cpp`
- Load a real CAN log file
- Process through complete pipeline
- Verify expected VSS outputs
- Test data files needed:
  - Sample candump.log
  - Corresponding DBC file
  - Expected output for validation

### 2. **End-to-End Test**
File: `test_end_to_end.cpp`
- Simulate complete workflow
- Test with Tesla/Battery examples
- Verify struct outputs
- Performance benchmarking

## Test Data Requirements

### For Unit Tests
Located in: `tests/data/`

1. **DBC Files** (`tests/data/dbc/`)
   - `minimal.dbc` - Minimal valid DBC
   - `complex.dbc` - Multi-message with enums
   - `invalid.dbc` - For error testing

2. **YAML Mappings** (`tests/data/yaml/`)
   - `simple_mappings.yaml` - Basic transformations
   - `complex_dag.yaml` - Multi-level dependencies
   - `struct_mappings.yaml` - Struct outputs

3. **CAN Logs** (`tests/data/canlogs/`)
   - `sample_candump.log` - Real CAN data
   - `synthetic_test.log` - Known values for testing

## Running Tests

```bash
# Build with tests enabled (default)
./build.sh

# Run all tests
cd build
ctest

# Run specific test suite
./tests/test_vss_types
./tests/test_signal_dag

# Run with verbose output
ctest -V

# Run only unit tests
ctest -R "^test_" -E "integration"

# Run only integration tests
ctest -R "integration"
```

## Coverage Goals

- Unit tests: Aim for >80% code coverage
- Integration tests: Cover all major use cases
- Performance tests: Establish baselines for:
  - Signal processing throughput
  - Memory usage
  - Lua execution overhead

## Continuous Testing

- Tests are built by default to ensure they stay up-to-date
- Run tests before every commit
- Add tests for every new feature
- Add regression tests for every bug fix

## Next Steps

1. **Immediate**: Fix compilation of existing unit tests
2. **Short term**: Add test data files
3. **Medium term**: Implement integration tests
4. **Long term**: Add performance benchmarks and fuzzing
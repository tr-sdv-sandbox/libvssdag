#!/bin/bash

# Build script for libVSSDAG
# Kept in root directory for convenience

mkdir -p build 
cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON
make -j$(nproc)

echo ""
echo "Build complete!"
echo "Library: build/libvssdag.a"
echo "Example: build/examples/can_transformer/can-transformer"
echo ""
echo "Tests built in: build/tests/"
echo ""
echo "To run tests:"
echo "  cd build && ctest"
echo ""
echo "To run the example:"
echo "  ./build/examples/can_transformer/can-transformer <dbc_file> <mapping_yaml> <can_interface>"



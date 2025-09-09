#!/bin/bash

rm -rf build
mkdir -p build 
cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON
make -j8



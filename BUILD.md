# Building libvssdag on Ubuntu 24.04

## Install System Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libgoogle-glog-dev \
    liblua5.3-dev \
    nlohmann-json3-dev \
    libyaml-cpp-dev \
    can-utils

# For testing (optional)
sudo apt-get install -y \
    libgtest-dev \
    libgmock-dev
```

## Build and Install Required Dependencies

### 1. Build and Install concurrentqueue

```bash
cd /tmp
git clone --depth 1 https://github.com/cameron314/concurrentqueue.git
cd concurrentqueue
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
sudo cmake --install .
```

### 2. Build and Install dbcppp

```bash
cd /tmp
git clone --depth 1 https://github.com/xR3b0rn/dbcppp.git
cd dbcppp
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -Dbuild_kcd=OFF \
      -Dbuild_tools=OFF \
      -Dbuild_tests=OFF \
      -Dbuild_examples=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

### 3. Build and Install libvss-types

```bash
cd /tmp
git clone --depth 1 https://github.com/tr-sdv-sandbox/libvss-types.git
cd libvss-types
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DVSS_TYPES_BUILD_TESTS=OFF \
      -DVSS_TYPES_BUILD_EXAMPLES=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Build libvssdag

```bash
cd ~/path/to/libvssdag
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/usr/local \
      ..
make -j$(nproc)
```

The library will be at `build/libvssdag.a` (or `libvssdag.so` if built as shared).

## Build Options

- `BUILD_EXAMPLES` - Build example applications (default: ON)
- `BUILD_TESTS` - Build unit tests (default: ON)
- `BUILD_SHARED_LIBS` - Build shared library instead of static (default: OFF)
- `BUILD_INTEGRATION_TESTS` - Build integration tests (default: ON)

Example with custom options:

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=OFF \
      -DBUILD_TESTS=OFF \
      -DBUILD_SHARED_LIBS=ON \
      ..
```

## Optional: Install libvssdag System-Wide

```bash
sudo make install
```

This will install:
- Library to `/usr/local/lib/libvssdag.a` (or `.so`)
- Headers to `/usr/local/include/vssdag/`
- CMake config files to `/usr/local/lib/cmake/vssdag/`

## Run Tests

```bash
cd build
ctest --output-on-failure
```

## Set Up Virtual CAN Interface (for testing/examples)

```bash
# Load vcan kernel module
sudo modprobe vcan

# Create vcan0 interface
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Verify it's up
ip link show vcan0
```

## Usage in Your CMake Project

After installation, you can use libvssdag in your project:

```cmake
cmake_minimum_required(VERSION 3.14)
project(my_project)

find_package(vssdag REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE vssdag::vssdag)
```

## Troubleshooting

### CMake can't find dependencies

Make sure `/usr/local/lib/cmake` is in your CMAKE_PREFIX_PATH:
```bash
export CMAKE_PREFIX_PATH=/usr/local:$CMAKE_PREFIX_PATH
```

### Missing vcan interface

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### dbcppp not found

Ensure dbcppp is installed and in a standard location. Check with:
```bash
ls -l /usr/local/lib/libdbcppp.so
ls -l /usr/local/include/dbcppp/
```

If installed elsewhere, add to CMAKE_PREFIX_PATH:
```bash
cmake -DCMAKE_PREFIX_PATH="/path/to/dbcppp;/usr/local" ..
```

## Clean Build

To rebuild from scratch:
```bash
cd build
rm -rf *
cmake ..
make -j$(nproc)
```

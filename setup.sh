# Build and Install concurrentqueue
cd /tmp
git clone --depth 1 https://github.com/cameron314/concurrentqueue.git
cd concurrentqueue
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
cmake --install .

# Build and Install dbcppp
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
make install
ldconfig

# Build and Install Open1722 (AVTP support)
cd /tmp
git clone --depth 1 https://github.com/COVESA/Open1722.git
cd Open1722
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
make install
ldconfig

# Build and Install libvss-types
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
make install
ldconfig

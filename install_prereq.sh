#!/bin/bash

# Core development dependencies
sudo apt-get update
sudo apt-get -y install \
    build-essential \
    cmake \
    libgoogle-glog-dev \
    liblua5.4-dev \
    nlohmann-json3-dev \
    libyaml-cpp-dev \
    libxml2-dev

# Testing dependencies (optional but recommended)
sudo apt-get -y install \
    libgtest-dev \
    libgmock-dev

# Runtime dependencies for CAN support
sudo apt-get -y install can-utils

echo "Prerequisites installed successfully!"

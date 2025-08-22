#!/bin/bash

# Development dependencies
sudo apt-get -y install libgoogle-glog-dev liblua5.4-dev nlohmann-json3-dev libyaml-cpp0.8 libxml2-dev

# Testing dependencies
sudo apt-get -y install libgtest-dev libgmock-dev

# Runtime dependencies
sudo apt-get -y install can-utils

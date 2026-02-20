# Vehicle Edge Platform - Development Container
# Ubuntu 24.04 with all build dependencies

FROM ubuntu:24.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential \
    cmake \
    pkg-config \
    libgoogle-glog-dev \
    nlohmann-json3-dev \
    libyaml-cpp-dev \
    can-utils \
    libprotobuf32t64 \
    libgrpc++1.51t64 \
    libgflags2.2 \
    libyaml-cpp0.8 \
    liblua5.3-dev \
    libpaho-mqtt1.3 \
    libpaho-mqttpp3-1 \
    libmosquitto1 \
    netcat-openbsd \
    libgtest-dev \
    libgmock-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspaces/vehicle-edge-platform

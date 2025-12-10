#!/bin/bash
# Run AVTP loopback tests with proper network setup
#
# This script creates a veth pair for loopback testing and runs the
# AVTP integration tests with the necessary permissions.
#
# Usage:
#   sudo ./run_avtp_tests.sh [test_binary_path]
#
# Requirements:
#   - Root privileges (for creating veth pair and raw sockets)
#   - iproute2 (ip command)

set -e

# Default test binary path
TEST_BINARY="${1:-./tests/test_avtp_loopback}"

# Check for root
if [ "$EUID" -ne 0 ]; then
    echo "This script requires root privileges for:"
    echo "  - Creating veth pair for loopback"
    echo "  - Running raw socket tests"
    echo ""
    echo "Usage: sudo $0 [test_binary_path]"
    exit 1
fi

# Check test binary exists
if [ ! -f "$TEST_BINARY" ]; then
    echo "Error: Test binary not found: $TEST_BINARY"
    echo "Please build the tests first: make test_avtp_loopback"
    exit 1
fi

echo "=== AVTP Loopback Test Setup ==="

# Create veth pair if it doesn't exist
VETH_A="avtp_test0"
VETH_B="avtp_test1"

cleanup() {
    echo "Cleaning up veth pair..."
    ip link delete "$VETH_A" 2>/dev/null || true
}

# Set up cleanup trap
trap cleanup EXIT

# Create veth pair
echo "Creating veth pair: $VETH_A <-> $VETH_B"
ip link add "$VETH_A" type veth peer name "$VETH_B"
ip link set "$VETH_A" up
ip link set "$VETH_B" up

# Set promiscuous mode to receive all packets
ip link set "$VETH_A" promisc on
ip link set "$VETH_B" promisc on

echo "veth pair created and up"
echo ""

# Run the tests with the veth interfaces
echo "=== Running AVTP Tests ==="
echo "TX Interface: $VETH_A"
echo "RX Interface: $VETH_B"
echo ""

# Export the interface names for the test
# Sender uses VETH_A, receiver uses VETH_B (packets flow through the veth pair)
export AVTP_TEST_INTERFACE="$VETH_A"
export AVTP_TEST_INTERFACE_RX="$VETH_B"

# Run the test
"$TEST_BINARY" --gtest_filter="*"

echo ""
echo "=== Test Complete ==="

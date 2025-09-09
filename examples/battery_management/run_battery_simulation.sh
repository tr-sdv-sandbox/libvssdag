#!/bin/bash

# Script to run CAN to VSS DAG transformation for battery simulation
# Usage: ./run_battery_simulation.sh [scenario]

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/../.."

echo -e "${GREEN}=== Battery Cell Aggregation Simulation ===${NC}"
echo ""
echo "This demo shows different strategies for handling slowly arriving battery cell data:"
echo "  1. Last Known Value (LKV) - Always complete, uses old values"
echo "  2. Windowed - Shows which cells are fresh/stale"  
echo "  3. Complete Cycle - Only emits when all cells updated"
echo ""

# Check if virtual CAN interface exists
if ! ip link show vcan0 &> /dev/null; then
    echo -e "${YELLOW}Creating virtual CAN interface vcan0...${NC}"
    sudo ip link add dev vcan0 type vcan
    sudo ip link set up vcan0
fi

# Configuration
VCAN_INTERFACE="${1:-vcan0}"
DBC_FILE="$SCRIPT_DIR/BatterySimulation.dbc"
MAPPING_FILE="$SCRIPT_DIR/battery_cells_mapping.yaml"
TRANSFORMER_BIN="../../build/examples/can_transformer/can-transformer"

# Check if binary exists
if [ ! -f "$TRANSFORMER_BIN" ]; then
    echo -e "${RED}Error: CAN TRANSFORMER binary not found: $TRANSFORMER_BIN${NC}"
    echo -e "${YELLOW}Please build the project first:${NC}"
    echo "  cd $PROJECT_ROOT"
    echo "  ./build.sh"
    exit 1
fi

# Check if DBC file exists
if [ ! -f "$DBC_FILE" ]; then
    echo -e "${RED}Error: DBC file not found: $DBC_FILE${NC}"
    exit 1
fi

# Check if mapping file exists
if [ ! -f "$MAPPING_FILE" ]; then
    echo -e "${RED}Error: Mapping file not found: $MAPPING_FILE${NC}"
    exit 1
fi

# Display configuration
echo -e "${BLUE}Configuration:${NC}"
echo "  CAN Interface: $VCAN_INTERFACE"
echo "  DBC File:      $(basename $DBC_FILE)"
echo "  Mapping File:  $(basename $MAPPING_FILE)"
echo ""

# Start CAN simulator in background
echo -e "${GREEN}Starting battery cell simulator...${NC}"
$SCRIPT_DIR/simulate_battery_cells.sh normal &
SIMULATOR_PID=$!

# Set up cleanup
cleanup() {
    echo -e "\n${YELLOW}Shutting down...${NC}"
    kill $SIMULATOR_PID 2>/dev/null
    exit 0
}
trap cleanup INT TERM

# Set environment variables for logging
export GLOG_logtostderr=1
export GLOG_colorlogtostderr=1
export GLOG_v=${GLOG_v:-1}  # Default to verbose

echo -e "${GREEN}Starting CAN to VSS converter...${NC}"
echo "Watch for these struct outputs:"
echo "  - CellsLKV: Updates with every new cell value"
echo "  - CellsWindowed: Shows cells within freshness window"
echo "  - CellsComplete: Only when all cells are fresh"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Run the converter and highlight struct outputs
$TRANSFORMER_BIN "$DBC_FILE" "$MAPPING_FILE" "$VCAN_INTERFACE" 2>&1 | \
    grep --line-buffered -E "(VSS:.*Cells|VSS:.*Min|VSS:.*Max|Battery|cells|ERROR|WARNING)" | \
    sed -e "s/\(CellsLKV\)/$(printf '\033[32m')\1$(printf '\033[0m')/g" \
        -e "s/\(CellsWindowed\)/$(printf '\033[33m')\1$(printf '\033[0m')/g" \
        -e "s/\(CellsComplete\)/$(printf '\033[36m')\1$(printf '\033[0m')/g"

echo -e "\n${GREEN}Battery simulation stopped${NC}"

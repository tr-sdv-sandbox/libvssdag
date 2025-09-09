#!/bin/bash

# Script to run CAN to VSS DAG transformation
# Usage: ./run_can_to_vss_dag.sh [vcan_interface] [dbc_file] [mapping_file]

# Default values
VCAN_INTERFACE="${1:-vcan0}"
DBC_FILE="${2:-Model3CAN.dbc}"
MAPPING_FILE="${3:-model3_mappings_dag.yaml}"

if ! ip link show $VCAN_INTERFACE &> /dev/null; then
  sudo ip link add dev $VCAN_INTERFACE type vcan
  sudo ip link set up $VCAN_INTERFACE 
fi

TRANSFORMER_BIN="../../build/examples/can_transformer/can-transformer"

# Output log file
mkdir -p logs
LOG_FILE="logs/can_to_vss_dag_$(date +%Y%m%d_%H%M%S).log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== CAN TRANSFORMER ===${NC}"

# Check if binary exists
if [ ! -f "$TRANSFORMER_BIN" ]; then
    echo -e "${RED}Error: CAN TRANSFORMER binary not found: ${NC}"
    echo -e "${YELLOW}Please build the project first:${NC}"
    echo "  cd build"
    echo "  cmake .."
    echo "  make "
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
echo -e "\n${BLUE}Configuration:${NC}"
echo "  CAN Interface: $VCAN_INTERFACE"
echo "  DBC File:      $DBC_FILE"
echo "  Mapping File:  $MAPPING_FILE"
echo "  Log File:      $LOG_FILE"
echo ""
echo -e "${BLUE}Tips:${NC}"
echo "  - Run with GLOG_v=1 for verbose output"
echo "  - Run with GLOG_v=2 to see signal processing details"
echo "  - Run with GLOG_v=3 to see all CAN frames"
echo ""
echo "Example: GLOG_v=2 ./run_can_to_vss_dag.sh"

# Set up signal handler
trap 'echo -e "\n${YELLOW}Shutting down CAN to VSS DAG converter...${NC}"; exit 0' INT TERM

# Run the converter
echo -e "\n${GREEN}Starting CAN to VSS DAG converter...${NC}"
echo "This version supports multi-signal dependencies and stateful transformations!"
echo "Press Ctrl+C to stop"
echo ""

# Set environment variables for logging
export GLOG_logtostderr=1
export GLOG_colorlogtostderr=1

# Allow verbosity to be set via environment or use default
if [ -z "$GLOG_v" ]; then
    export GLOG_v=0  # Default verbosity level (0=normal, 1-3=debug)
fi

# Show current verbosity level
case $GLOG_v in
    0) echo -e "${BLUE}Logging level: Normal${NC}" ;;
    1) echo -e "${YELLOW}Logging level: Verbose${NC}" ;;
    2) echo -e "${YELLOW}Logging level: Very Verbose${NC}" ;;
    3) echo -e "${YELLOW}Logging level: Extremely Verbose${NC}" ;;
    *) echo -e "${YELLOW}Logging level: Custom ($GLOG_v)${NC}" ;;
esac

# Run and tee output to both console and log file
$TRANSFORMER_BIN "$DBC_FILE" "$MAPPING_FILE" "$VCAN_INTERFACE" 2>&1 | tee "$LOG_FILE"

# Alternative: Run with only VSS output visible (no debug logs)
# To use this, comment out the line above and uncomment below:
# $CAN_TO_VSS_BIN "$DBC_FILE" "$MAPPING_FILE" "$VCAN_INTERFACE" 2>&1 | \
#     grep "VSS:" | tee "$LOG_FILE"

echo -e "\n${GREEN}CAN to VSS DAG converter stopped${NC}"
echo -e "${BLUE}Log saved to: $LOG_FILE${NC}"

#!/bin/bash

# Script to replay Tesla CAN log data
# Usage: ./run_can_replay.sh [vcan_interface] [log_file] [replay_speed]

# Default values
VCAN_INTERFACE="${1:-vcan0}"
CAN_LOG="${2:-candump.log}"
REPLAY_SPEED="${3:-1}"  # 1=realtime, 2=2x speed, 0.5=half speed, etc.

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Tesla CAN Log Replay ===${NC}"

# Check if log file exists
if [ ! -f "$CAN_LOG" ]; then
    echo -e "${RED}Error: CAN log file not found: $CAN_LOG${NC}"
    exit 1
fi

# Check if vcan module is loaded
if ! lsmod | grep -q "^vcan"; then
    echo -e "${YELLOW}Loading vcan kernel module...${NC}"
    sudo modprobe vcan
    if [ $? -ne 0 ]; then
        echo -e "${RED}Error: Failed to load vcan module${NC}"
        exit 1
    fi
fi

# Check if virtual CAN interface exists
if ! ip link show $VCAN_INTERFACE &> /dev/null; then
    echo -e "${YELLOW}Creating virtual CAN interface: $VCAN_INTERFACE${NC}"
    sudo ip link add dev $VCAN_INTERFACE type vcan
    sudo ip link set up $VCAN_INTERFACE
    if [ $? -ne 0 ]; then
        echo -e "${RED}Error: Failed to create virtual CAN interface${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}Using existing virtual CAN interface: $VCAN_INTERFACE${NC}"
fi

# Show CAN interface status
echo -e "\n${YELLOW}CAN Interface Status:${NC}"
ip -details link show $VCAN_INTERFACE

# Count messages in log
MSG_COUNT=$(wc -l < "$CAN_LOG")
echo -e "\n${GREEN}Ready to replay $MSG_COUNT CAN messages from:${NC}"
echo "  $CAN_LOG"
echo -e "${GREEN}To interface:${NC} $VCAN_INTERFACE"

# Start replay
echo -e "\n${YELLOW}Starting CAN replay...${NC}"
echo "Press Ctrl+C to stop"

# Run canplayer
# -I flag specifies input file
# Default behavior (no -t flag) preserves original timing from log
# -t flag ignores timestamps and sends immediately
# vcan0=elmcan maps elmcan interface from log to vcan0

if [ "$REPLAY_SPEED" == "0" ]; then
    echo "Replaying as fast as possible (no timing)"
    canplayer -t -I "$CAN_LOG" ${VCAN_INTERFACE}=elmcan
else
    # For realtime or scaled replay, we use the timestamps from the log
    # Unfortunately canplayer doesn't support speed scaling, so we can only do 1x or instant
    if [ "$REPLAY_SPEED" == "1" ]; then
        echo "Replaying in realtime (preserving original timing)"
        canplayer -I "$CAN_LOG" ${VCAN_INTERFACE}=elmcan
    else
        echo "Warning: canplayer doesn't support speed scaling. Using realtime mode."
        echo "Use 0 for fast replay or 1 for realtime."
        canplayer -I "$CAN_LOG" ${VCAN_INTERFACE}=elmcan
    fi
fi

echo -e "\n${GREEN}CAN replay completed${NC}"

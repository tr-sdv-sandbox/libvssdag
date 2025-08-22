#!/bin/bash

# Simulates slowly arriving battery cell data on CAN bus
# Shows how different struct aggregation strategies handle the data

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}=== Battery Cell Data Simulation ===${NC}"
echo "This simulates battery cells being scanned sequentially"
echo "Full cycle takes ~20 seconds for 96 cells"
echo ""

# Check if vcan0 exists
if ! ip link show vcan0 &> /dev/null; then
    echo -e "${YELLOW}Creating virtual CAN interface vcan0...${NC}"
    sudo ip link add dev vcan0 type vcan
    sudo ip link set up vcan0
fi

# Function to send cell voltage group
send_cell_group() {
    local group=$1
    local base_voltage=3650  # 3.650V in mV, raw value for DBC scaling
    
    # Add some variation to voltages
    local v1=$((base_voltage + RANDOM % 100))
    local v2=$((base_voltage + RANDOM % 100))
    local v3=$((base_voltage + RANDOM % 100))
    
    # Pack 3 cell voltages into one CAN message
    # DBC expects little-endian 16-bit values that get scaled by 0.001
    # Convert to little-endian hex
    local v1_hex=$(printf "%02X%02X" $((v1 & 0xFF)) $((v1 >> 8)))
    local v2_hex=$(printf "%02X%02X" $((v2 & 0xFF)) $((v2 >> 8)))
    local v3_hex=$(printf "%02X%02X" $((v3 & 0xFF)) $((v3 >> 8)))
    
    # Only 6 bytes used for 3x 16-bit values
    local data="${v1_hex}${v2_hex}${v3_hex}0000"
    
    case $group in
        1)
            can_id=$(printf "%03X" 594)  # DBC uses 594 for Cells 1-3
            signal="BMS_CellVoltages_1_3"
            ;;
        2)
            can_id=$(printf "%03X" 610)  # DBC uses 610 for Cells 4-6  
            signal="BMS_CellVoltages_4_6"
            ;;
        3)
            can_id=$(printf "%03X" 626)  # DBC uses 626 for Cells 7-9
            signal="BMS_CellVoltages_7_9"
            ;;
        4)
            can_id=$(printf "%03X" 642)  # DBC uses 642 for Cells 10-12
            signal="BMS_CellVoltages_10_12"
            ;;
        *)
            can_id="2A0"
            signal="Unknown"
            ;;
    esac
    
    echo -e "${BLUE}[Group $group] Sending $signal: Cell voltages $v1, $v2, $v3 mV${NC}"
    cansend vcan0 "${can_id}#${data}"
}

# Function to send min/max cell voltages
send_min_max() {
    local min=$((3580 + RANDOM % 50))  # Min around 3.58-3.63V
    local max=$((3700 + RANDOM % 50))  # Max around 3.70-3.75V
    local min_id=$((RANDOM % 12))      # Random cell has min
    local max_id=$((RANDOM % 12))      # Random cell has max
    local delta=$((max - min))
    
    # Send BMS_CellStatistics message (CAN ID 818)
    # Format: min(2), max(2), delta(2) - little-endian
    local min_hex=$(printf "%02X%02X" $((min & 0xFF)) $((min >> 8)))
    local max_hex=$(printf "%02X%02X" $((max & 0xFF)) $((max >> 8)))
    local delta_hex=$(printf "%02X%02X" $((delta & 0xFF)) $((delta >> 8)))
    local data="${min_hex}${max_hex}${delta_hex}0000"
    local can_id=$(printf "%03X" 818)  # DBC uses 818
    cansend vcan0 "${can_id}#${data}"
    
    echo -e "${GREEN}[Stats] Min: ${min}mV (Cell $min_id), Max: ${max}mV (Cell $max_id), Delta: ${delta}mV${NC}"
}

# Simulation scenarios
case ${1:-normal} in
    normal)
        echo -e "${GREEN}Scenario: Normal scanning (groups arrive sequentially)${NC}"
        echo "Each group takes ~5 seconds, full cycle ~20 seconds"
        echo ""
        
        for cycle in {1..3}; do
            echo -e "${YELLOW}=== Cycle $cycle ===${NC}"
            
            # Send groups in order: 1, 2, 3, 4
            for group in 1 2 3 4; do
                send_cell_group $group
                sleep 5  # 5 seconds between groups (20s total)
            done
            
            send_min_max
            echo -e "${GREEN}Cycle $cycle complete${NC}"
            echo ""
            sleep 2
        done
        ;;
        
    fast)
        echo -e "${GREEN}Scenario: Fast scanning (all groups quickly)${NC}"
        echo "All groups sent within 4 seconds"
        echo ""
        
        for cycle in {1..5}; do
            echo -e "${YELLOW}=== Fast Cycle $cycle ===${NC}"
            
            for group in {1..4}; do
                send_cell_group $group
                sleep 1  # Only 1 second between groups
            done
            
            send_min_max
            echo -e "${GREEN}Fast cycle $cycle complete${NC}"
            sleep 5
        done
        ;;
        
    missing)
        echo -e "${RED}Scenario: Missing data (group 2 never arrives)${NC}"
        echo "Simulates communication fault on one cell group"
        echo ""
        
        for cycle in {1..3}; do
            echo -e "${YELLOW}=== Faulty Cycle $cycle ===${NC}"
            
            send_cell_group 1
            sleep 5
            
            echo -e "${RED}[MISSING] Group 2 data not received${NC}"
            sleep 5
            
            send_cell_group 3
            sleep 5
            
            send_cell_group 4
            sleep 5
            
            send_min_max
            echo -e "${YELLOW}Cycle $cycle incomplete (missing group 2)${NC}"
            echo ""
        done
        ;;
        
    random)
        echo -e "${GREEN}Scenario: Random arrival (groups arrive out of order)${NC}"
        echo "Simulates real-world async behavior"
        echo ""
        
        for cycle in {1..3}; do
            echo -e "${YELLOW}=== Random Cycle $cycle ===${NC}"
            
            # Random order
            order=(1 2 3 4)
            shuffled=($(shuf -e "${order[@]}"))
            
            for group in "${shuffled[@]}"; do
                send_cell_group $group
                sleep $((RANDOM % 10 + 2))  # Random 2-12 second delays
            done
            
            send_min_max
            echo -e "${GREEN}Random cycle $cycle complete${NC}"
            echo ""
        done
        ;;
        
    burst)
        echo -e "${GREEN}Scenario: Burst mode (rapid updates then silence)${NC}"
        echo "Simulates BMS doing rapid scan then sleeping"
        echo ""
        
        for burst in {1..3}; do
            echo -e "${YELLOW}=== Burst $burst ===${NC}"
            
            # Rapid fire all groups
            for i in {1..5}; do
                for group in {1..4}; do
                    send_cell_group $group
                    sleep 0.1  # 100ms between messages
                done
            done
            
            send_min_max
            echo -e "${GREEN}Burst $burst complete, sleeping 30s...${NC}"
            sleep 30
        done
        ;;
        
    sequential)
        echo -e "${GREEN}Scenario: Sequential cell scanning (clear cycle boundaries)${NC}"
        echo "Each group takes 2 seconds, cells scan in strict order"
        echo ""
        
        for cycle in {1..5}; do
            echo -e "${YELLOW}=== Sequential Cycle $cycle ===${NC}"
            
            # Send groups in strict order with short delays
            for group in 1 2 3 4; do
                send_cell_group $group
                sleep 2  # 2 seconds between groups
            done
            
            send_min_max
            echo -e "${GREEN}Sequential cycle $cycle complete${NC}"
            echo ""
            sleep 1  # Short pause before next cycle
        done
        ;;
        
    *)
        echo "Usage: $0 [normal|fast|missing|random|burst|sequential]"
        echo ""
        echo "Scenarios:"
        echo "  normal     - Groups arrive sequentially every 5 seconds"
        echo "  fast       - All groups arrive within 4 seconds"
        echo "  missing    - Group 2 never arrives (fault simulation)"
        echo "  random     - Groups arrive in random order with random delays"
        echo "  burst      - Rapid updates followed by long silence"
        echo "  sequential - Strict sequential scanning with clear cycles"
        exit 1
        ;;
esac

echo -e "${GREEN}Simulation complete${NC}"
echo ""
echo "To monitor the struct outputs, run in another terminal:"
echo "  ./run_can_to_vss_dag.sh | grep 'Battery.*Cells'"
echo ""
echo "This will show how different strategies handle the data:"
echo "  - CellsLKV: Updates continuously with last known values"
echo "  - CellsWindowed: Shows which cells are fresh/stale"
echo "  - CellsComplete: Only emits when all cells are fresh"
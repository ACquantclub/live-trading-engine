#!/bin/bash

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building Live Trading Engine...${NC}"

# Create build directory
BUILD_DIR="build"
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p "$BUILD_DIR"
fi

# Create logs directory
LOGS_DIR="logs"
if [ ! -d "$LOGS_DIR" ]; then
    echo -e "${YELLOW}Creating logs directory...${NC}"
    mkdir -p "$LOGS_DIR"
fi

# Navigate to build directory
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${YELLOW}Configuring project with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=23

# Build the project
echo -e "${YELLOW}Building project...${NC}"
make -j$(nproc)

echo -e "${GREEN}Build completed successfully!${NC}"

# Display build artifacts
echo -e "${YELLOW}Build artifacts:${NC}"
echo "  - Trading Engine: $(pwd)/apps/trading_engine/trading_engine"
if [ -f "unit_tests" ]; then
    echo "  - Unit Tests: $(pwd)/unit_tests"
fi
if [ -f "integration_tests" ]; then
    echo "  - Integration Tests: $(pwd)/integration_tests"
fi

echo -e "${GREEN}Ready to run!${NC}"
echo "To start the trading engine: ./build/apps/trading_engine/trading_engine" 
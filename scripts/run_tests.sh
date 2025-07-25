#!/bin/bash

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Running Trading Engine Tests...${NC}"

# Ensure build directory exists
BUILD_DIR="build"
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Build directory not found. Please run ./scripts/build.sh first${NC}"
    exit 1
fi

# Check if we have any tests to run
UNIT_TEST_PATH="$BUILD_DIR/tests/unit_tests"
INTEGRATION_TEST_PATH="$BUILD_DIR/tests/integration_tests"

# Run unit tests if they exist
if [ -f "$UNIT_TEST_PATH" ]; then
    echo -e "${YELLOW}Running unit tests...${NC}"
    cd "$BUILD_DIR"
    ./tests/unit_tests --gtest_output=xml:unit_test_results.xml
    UNIT_TEST_RESULT=$?
    cd ..
    
    if [ $UNIT_TEST_RESULT -eq 0 ]; then
        echo -e "${GREEN}Unit tests passed!${NC}"
    else
        echo -e "${RED}Unit tests failed!${NC}"
    fi
else
    echo -e "${YELLOW}No unit tests found at $UNIT_TEST_PATH${NC}"
    UNIT_TEST_RESULT=0
fi

# Run integration tests if they exist
if [ -f "$INTEGRATION_TEST_PATH" ]; then
    echo -e "${YELLOW}Running integration tests...${NC}"
    cd "$BUILD_DIR"
    ./tests/integration_tests --gtest_output=xml:integration_test_results.xml
    INTEGRATION_TEST_RESULT=$?
    cd ..
    
    if [ $INTEGRATION_TEST_RESULT -eq 0 ]; then
        echo -e "${GREEN}Integration tests passed!${NC}"
    else
        echo -e "${RED}Integration tests failed!${NC}"
    fi
else
    echo -e "${YELLOW}No integration tests found at $INTEGRATION_TEST_PATH${NC}"
    INTEGRATION_TEST_RESULT=0
fi

# Alternative: Use CTest (recommended approach)
echo -e "${YELLOW}Running tests via CTest...${NC}"
cd "$BUILD_DIR"
ctest --output-on-failure
CTEST_RESULT=$?
cd ..

if [ $CTEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}CTest passed!${NC}"
else
    echo -e "${RED}CTest failed!${NC}"
fi

# Overall result
if [ $UNIT_TEST_RESULT -eq 0 ] && [ $INTEGRATION_TEST_RESULT -eq 0 ] && [ $CTEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi 
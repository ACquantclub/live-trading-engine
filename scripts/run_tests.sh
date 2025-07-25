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

cd "$BUILD_DIR"

# Run unit tests if they exist
if [ -f "unit_tests" ]; then
    echo -e "${YELLOW}Running unit tests...${NC}"
    ./unit_tests --gtest_output=xml:unit_test_results.xml
    UNIT_TEST_RESULT=$?
    
    if [ $UNIT_TEST_RESULT -eq 0 ]; then
        echo -e "${GREEN}Unit tests passed!${NC}"
    else
        echo -e "${RED}Unit tests failed!${NC}"
    fi
else
    echo -e "${YELLOW}No unit tests found to run${NC}"
    UNIT_TEST_RESULT=0
fi

# Run integration tests if they exist
if [ -f "integration_tests" ]; then
    echo -e "${YELLOW}Running integration tests...${NC}"
    ./integration_tests --gtest_output=xml:integration_test_results.xml
    INTEGRATION_TEST_RESULT=$?
    
    if [ $INTEGRATION_TEST_RESULT -eq 0 ]; then
        echo -e "${GREEN}Integration tests passed!${NC}"
    else
        echo -e "${RED}Integration tests failed!${NC}"
    fi
else
    echo -e "${YELLOW}No integration tests found to run${NC}"
    INTEGRATION_TEST_RESULT=0
fi

# Overall result
if [ $UNIT_TEST_RESULT -eq 0 ] && [ $INTEGRATION_TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi 
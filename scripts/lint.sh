#!/bin/bash

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo -e "${GREEN}Trading Engine Static Code Analysis${NC}"
echo -e "${BLUE}===================================${NC}"

# Check if clang-tidy is installed
if ! command -v clang-tidy &> /dev/null; then
    echo -e "${RED}Error: clang-tidy is not installed${NC}"
    echo "Please install clang-tidy:"
    echo "  Ubuntu/Debian: sudo apt-get install clang-tidy"
    echo "  macOS: brew install llvm"
    exit 1
fi

# Default options
FIX_ERRORS=false
VERBOSE=false
BUILD_DIR="build"
PARALLEL_JOBS=$(nproc 2>/dev/null || echo "4")

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "OPTIONS:"
            echo "  -f, --fix         Auto-fix issues where possible"
            echo "  -v, --verbose     Show detailed output"
            echo "  -j, --jobs N      Number of parallel jobs (default: $PARALLEL_JOBS)"
            echo "  -b, --build DIR   Build directory (default: build)"
            echo "  -h, --help        Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                Run static analysis"
            echo "  $0 --fix          Run analysis and auto-fix issues"
            echo "  $0 -j 8           Use 8 parallel jobs"
            exit 0
            ;;
        -f|--fix)
            FIX_ERRORS=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -j|--jobs)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        -b|--build)
            BUILD_DIR="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

cd "$PROJECT_ROOT"

# Check if .clang-tidy exists
if [ ! -f ".clang-tidy" ]; then
    echo -e "${RED}Error: .clang-tidy configuration not found${NC}"
    exit 1
fi

# Check if build directory exists and has compile_commands.json
COMPILE_DB="$BUILD_DIR/compile_commands.json"
if [ ! -f "$COMPILE_DB" ]; then
    echo -e "${YELLOW}Warning: compile_commands.json not found${NC}"
    echo "Generating compile database..."
    
    # Ensure build directory exists
    mkdir -p "$BUILD_DIR"
    
    # Generate compile commands
    cd "$BUILD_DIR"
    cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_STANDARD=23 ..
    cd "$PROJECT_ROOT"
    
    if [ ! -f "$COMPILE_DB" ]; then
        echo -e "${RED}Error: Failed to generate compile database${NC}"
        echo "Please run: cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B build"
        exit 1
    fi
fi

echo -e "${YELLOW}Using compile database: $COMPILE_DB${NC}"

# Find source files to analyze
SOURCE_FILES=$(find include src apps -name "*.cpp" -o -name "*.hpp" | \
               grep -v build/ | \
               grep -v third_party/ | \
               grep -v deps/csp/ | \
               sort)

if [ -z "$SOURCE_FILES" ]; then
    echo -e "${YELLOW}No source files found to analyze${NC}"
    exit 0
fi

echo -e "${YELLOW}Found $(echo "$SOURCE_FILES" | wc -l) source files to analyze${NC}"

if [ "$VERBOSE" = true ]; then
    echo -e "${BLUE}Files to analyze:${NC}"
    echo "$SOURCE_FILES" | sed 's/^/  /'
fi

# Run clang-tidy
CLANG_TIDY_CMD="clang-tidy"
CLANG_TIDY_ARGS="-p $BUILD_DIR"

if [ "$FIX_ERRORS" = true ]; then
    CLANG_TIDY_ARGS="$CLANG_TIDY_ARGS --fix --fix-errors"
    echo -e "${YELLOW}Running with auto-fix enabled${NC}"
fi

if [ "$VERBOSE" = true ]; then
    CLANG_TIDY_ARGS="$CLANG_TIDY_ARGS --format-style=file"
fi

# Create temporary file for results
TEMP_RESULTS=$(mktemp)
trap "rm -f $TEMP_RESULTS" EXIT

echo -e "${BLUE}Running static analysis...${NC}"

# Run clang-tidy on all files
ERROR_COUNT=0
WARNING_COUNT=0
PROCESSED_COUNT=0

if command -v parallel &> /dev/null && [ "$PARALLEL_JOBS" -gt 1 ]; then
    echo -e "${YELLOW}Using $PARALLEL_JOBS parallel jobs${NC}"
    
    # Use GNU parallel for better performance
    echo "$SOURCE_FILES" | parallel -j "$PARALLEL_JOBS" --eta \
        "$CLANG_TIDY_CMD $CLANG_TIDY_ARGS {}" >> "$TEMP_RESULTS" 2>&1 || true
else
    # Sequential processing
    for file in $SOURCE_FILES; do
        echo -e "${BLUE}Analyzing: $file${NC}"
        $CLANG_TIDY_CMD $CLANG_TIDY_ARGS "$file" >> "$TEMP_RESULTS" 2>&1 || true
        ((PROCESSED_COUNT++))
    done
fi

# Parse results
if [ -s "$TEMP_RESULTS" ]; then
    if [ "$VERBOSE" = true ]; then
        echo -e "${BLUE}Detailed Results:${NC}"
        cat "$TEMP_RESULTS"
        echo ""
    fi
    
    # Count errors and warnings
    ERROR_COUNT=$(grep -c "error:" "$TEMP_RESULTS" 2>/dev/null || echo "0")
    WARNING_COUNT=$(grep -c "warning:" "$TEMP_RESULTS" 2>/dev/null || echo "0")
    
    echo -e "${BLUE}Analysis Summary:${NC}"
    echo -e "${YELLOW}Warnings: $WARNING_COUNT${NC}"
    echo -e "${RED}Errors: $ERROR_COUNT${NC}"
    
    if [ $ERROR_COUNT -gt 0 ]; then
        echo ""
        echo -e "${RED}Critical issues found:${NC}"
        grep "error:" "$TEMP_RESULTS" | head -10
        if [ $(grep -c "error:" "$TEMP_RESULTS") -gt 10 ]; then
            echo -e "${YELLOW}... and $((ERROR_COUNT - 10)) more errors${NC}"
        fi
    fi
    
    if [ $WARNING_COUNT -gt 0 ] && [ "$VERBOSE" = false ]; then
        echo ""
        echo -e "${YELLOW}Sample warnings (use --verbose for all):${NC}"
        grep "warning:" "$TEMP_RESULTS" | head -5
        if [ $WARNING_COUNT -gt 5 ]; then
            echo -e "${YELLOW}... and $((WARNING_COUNT - 5)) more warnings${NC}"
        fi
    fi
else
    echo -e "${GREEN}✓ No issues found!${NC}"
fi

# Exit with appropriate code
if [ $ERROR_COUNT -gt 0 ]; then
    echo ""
    echo -e "${RED}✗ Static analysis failed with $ERROR_COUNT error(s)${NC}"
    exit 1
elif [ $WARNING_COUNT -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}⚠ Static analysis completed with $WARNING_COUNT warning(s)${NC}"
    exit 0
else
    echo ""
    echo -e "${GREEN}✓ Static analysis passed successfully!${NC}"
    exit 0
fi 
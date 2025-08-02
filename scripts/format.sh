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

echo -e "${GREEN}Trading Engine Code Formatter${NC}"
echo -e "${BLUE}==============================${NC}"

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}Error: clang-format is not installed${NC}"
    echo "Please install clang-format:"
    echo "  Ubuntu/Debian: sudo apt-get install clang-format"
    echo "  macOS: brew install clang-format"
    exit 1
fi

# Check if .clang-format exists
if [ ! -f "$PROJECT_ROOT/.clang-format" ]; then
    echo -e "${RED}Error: .clang-format configuration not found${NC}"
    exit 1
fi

# Default mode
DRY_RUN=false
VERBOSE=false
CHECK_ONLY=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "OPTIONS:"
            echo "  -c, --check       Check if files are formatted (don't modify)"
            echo "  -d, --dry-run     Show what would be formatted (don't modify)"
            echo "  -v, --verbose     Show detailed output"
            echo "  -h, --help        Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                Format all C++ files"
            echo "  $0 --check        Check if files need formatting"
            echo "  $0 --dry-run      Preview formatting changes"
            exit 0
            ;;
        -c|--check)
            CHECK_ONLY=true
            shift
            ;;
        -d|--dry-run)
            DRY_RUN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

cd "$PROJECT_ROOT"

# Find all C++ source files
CPP_FILES=$(find . -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" | \
           grep -E '\.(cpp|hpp|h|cc)$' | \
           grep -v build/ | \
           grep -v third_party/ | \
           sort)

# Exclude apps/json.hpp
CPP_FILES=$(echo "$CPP_FILES" | grep -v "apps/json.hpp")

if [ -z "$CPP_FILES" ]; then
    echo -e "${YELLOW}No C++ files found to format${NC}"
    exit 0
fi

echo -e "${YELLOW}Found $(echo "$CPP_FILES" | wc -l) C++ files${NC}"

if [ "$VERBOSE" = true ]; then
    echo -e "${BLUE}Files to process:${NC}"
    echo "$CPP_FILES" | sed 's/^/  /'
fi

# Track formatting status
NEEDS_FORMATTING=()
FORMATTED_COUNT=0
ERROR_COUNT=0

for file in $CPP_FILES; do
    if [ "$VERBOSE" = true ]; then
        echo -e "${BLUE}Processing: $file${NC}"
    fi
    
    if [ "$CHECK_ONLY" = true ]; then
        # Check if file needs formatting
        if ! diff -q "$file" <(clang-format "$file") &>/dev/null; then
            NEEDS_FORMATTING+=("$file")
            if [ "$VERBOSE" = true ]; then
                echo -e "  ${YELLOW}Needs formatting${NC}"
            fi
        else
            if [ "$VERBOSE" = true ]; then
                echo -e "  ${GREEN}Already formatted${NC}"
            fi
        fi
    elif [ "$DRY_RUN" = true ]; then
        # Show diff without modifying
        if ! diff -u "$file" <(clang-format "$file") > /dev/null 2>&1; then
            echo -e "${YELLOW}Would format: $file${NC}"
            if [ "$VERBOSE" = true ]; then
                diff -u "$file" <(clang-format "$file") || true
            fi
            NEEDS_FORMATTING+=("$file")
        fi
    else
        # Actually format the file
        if clang-format -i "$file"; then
            FORMATTED_COUNT=$((FORMATTED_COUNT + 1))
            if [ "$VERBOSE" = true ]; then
                echo -e "  ${GREEN}Formatted${NC}"
            fi
        else
            ERROR_COUNT=$((ERROR_COUNT + 1))
            echo -e "  ${RED}Error formatting${NC}"
        fi
    fi
done

# Summary
echo ""
echo -e "${BLUE}Summary:${NC}"

if [ "$CHECK_ONLY" = true ]; then
    if [ ${#NEEDS_FORMATTING[@]} -eq 0 ]; then
        echo -e "${GREEN}✓ All files are properly formatted${NC}"
        exit 0
    else
        echo -e "${RED}✗ ${#NEEDS_FORMATTING[@]} file(s) need formatting:${NC}"
        for file in "${NEEDS_FORMATTING[@]}"; do
            echo -e "  ${RED}- $file${NC}"
        done
        exit 1
    fi
elif [ "$DRY_RUN" = true ]; then
    if [ ${#NEEDS_FORMATTING[@]} -eq 0 ]; then
        echo -e "${GREEN}✓ All files are properly formatted${NC}"
    else
        echo -e "${YELLOW}Would format ${#NEEDS_FORMATTING[@]} file(s)${NC}"
    fi
else
    echo -e "${GREEN}✓ Formatted $FORMATTED_COUNT file(s)${NC}"
    if [ $ERROR_COUNT -gt 0 ]; then
        echo -e "${RED}✗ $ERROR_COUNT error(s) occurred${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}Code formatting complete!${NC}" 
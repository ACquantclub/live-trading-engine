#!/bin/bash

# Trading Engine Pre-commit Hook
# This script runs code quality checks before each commit

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Running pre-commit checks...${NC}"

# Get the project root directory
PROJECT_ROOT="$(git rev-parse --show-toplevel)"
cd "$PROJECT_ROOT"

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${RED}Error: Not in a git repository${NC}"
    exit 1
fi

# Get list of staged files
STAGED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp|h|cc)$' | grep -v deps/csp/ || true)

if [ -z "$STAGED_FILES" ]; then
    echo -e "${GREEN}No C++ files staged for commit${NC}"
    exit 0
fi

echo -e "${YELLOW}Staged C++ files:${NC}"
echo "$STAGED_FILES" | sed 's/^/  /'

# Flag to track if we should abort the commit
ABORT_COMMIT=false

# 1. Check code formatting
echo ""
echo -e "${BLUE}1. Checking code formatting...${NC}"
if ./scripts/format.sh --check > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Code formatting check passed${NC}"
else
    echo -e "${RED}✗ Code formatting check failed${NC}"
    echo "Run: ./scripts/format.sh to fix formatting issues"
    ABORT_COMMIT=true
fi

# 2. Run static analysis on staged files only
echo ""
echo -e "${BLUE}2. Running static analysis...${NC}"

# Create temporary directory for analysis
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Check if compile_commands.json exists
if [ ! -f "build/compile_commands.json" ]; then
    echo -e "${YELLOW}Warning: compile_commands.json not found, skipping static analysis${NC}"
    echo "Run: cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_STANDARD=23 -B build"
else
    # Run clang-tidy on staged files in parallel
    LINT_ERRORS=0
    NPROC=$(nproc 2>/dev/null || echo "4")  # Default to 4 if nproc unavailable
    
    # Create a function to analyze a single file
    analyze_file() {
        local file="$1"
        local temp_dir="$2"
        if [ -f "$file" ]; then
            # Create safe filename for log (replace slashes with underscores)
            LOG_FILE="$temp_dir/lint_$(echo "$file" | tr '/' '_').log"
            if clang-tidy -p build "$file" > "$LOG_FILE" 2>&1; then
                echo "SUCCESS:$file"
            else
                echo "ERROR:$file"
            fi
        fi
    }
    export -f analyze_file
    
    # Use parallel processing if available, otherwise fall back to sequential
    if command -v parallel >/dev/null 2>&1; then
        echo -e "${BLUE}  Running parallel analysis with $NPROC jobs...${NC}"
        RESULTS=$(echo "$STAGED_FILES" | parallel -j "$NPROC" analyze_file {} "$TEMP_DIR")
    else
        echo -e "${BLUE}  Running sequential analysis...${NC}"
        RESULTS=""
        for file in $STAGED_FILES; do
            RESULTS="$RESULTS\n$(analyze_file "$file" "$TEMP_DIR")"
        done
    fi
    
    # Process results
    for result in $RESULTS; do
        if [[ "$result" == ERROR:* ]]; then
            file="${result#ERROR:}"
            LINT_ERRORS=$((LINT_ERRORS + 1))
            echo -e "${RED}  ✗ Issues found in $file${NC}"
            
            LOG_FILE="$TEMP_DIR/lint_$(echo "$file" | tr '/' '_').log"
            if [ -f "$LOG_FILE" ]; then
                head -10 "$LOG_FILE" | sed 's/^/    /'
            fi
        elif [[ "$result" == SUCCESS:* ]]; then
            file="${result#SUCCESS:}"
            echo -e "${GREEN}  ✓ $file${NC}"
        fi
    done
    
    if [ $LINT_ERRORS -gt 0 ]; then
        echo -e "${RED}✗ Static analysis found issues in $LINT_ERRORS file(s)${NC}"
        echo "Run: ./scripts/lint.sh --fix to auto-fix issues"
        ABORT_COMMIT=true
    else
        echo -e "${GREEN}✓ Static analysis passed${NC}"
    fi
fi

# 3. Check if build still works
echo ""
echo -e "${BLUE}3. Checking if code compiles...${NC}"
if [ -f "scripts/build.sh" ]; then
    if ./scripts/build.sh > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Build check passed${NC}"
    else
        echo -e "${RED}✗ Build check failed${NC}"
        echo "The staged changes break the build"
        ABORT_COMMIT=true
    fi
else
    echo -e "${YELLOW}Warning: build.sh not found, skipping build check${NC}"
fi

# Summary
echo ""
echo -e "${BLUE}Pre-commit check summary:${NC}"

if [ "$ABORT_COMMIT" = true ]; then
    echo -e "${RED}✗ Pre-commit checks failed${NC}"
    echo ""
    echo "To bypass these checks (not recommended), use:"
    echo "  git commit --no-verify"
    echo ""
    echo "To fix the issues:"
    echo "  ./scripts/format.sh    # Fix formatting"
    echo "  ./scripts/lint.sh      # Check static analysis"
    echo "  ./scripts/build.sh     # Test build"
    exit 1
else
    echo -e "${GREEN}✓ All pre-commit checks passed${NC}"
    echo -e "${GREEN}Proceeding with commit...${NC}"
    exit 0
fi 
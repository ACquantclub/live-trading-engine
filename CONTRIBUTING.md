# Contributing to Live Trading Engine

## Development Setup

### Prerequisites
- CMake 3.20+
- C++23 compatible compiler:
  - GCC 12+ (full C++23 support)
  - Clang 15+ (partial C++23 support, 17+ recommended)
- clang-format 15+ (for C++23 formatting)
- clang-tidy 15+ (for C++23 static analysis)

### Building the Project

```bash
# Build the project
./scripts/build.sh

# Run the trading engine
./build/apps/trading_engine/trading_engine

# Run tests
./scripts/run_tests.sh
```

## Development Scripts

The project includes several utility scripts to streamline development workflow:

### Build Script
```bash
# Build the entire project with optimizations
./scripts/build.sh

# The script will:
# - Create build directory if it doesn't exist
# - Configure CMake with Release build type and C++23 standard
# - Build using all available CPU cores
# - Display build artifacts locations
```

### Testing Scripts
```bash
# Run all tests (unit + integration)
./scripts/run_tests.sh

# Run trading simulation for testing
./scripts/run_simulation.sh

# Performance benchmarking
./scripts/throughput_eval.sh

# Test HTTP endpoints
./scripts/test_endpoints.sh
```

The `run_tests.sh` script executes both unit and integration tests, while `run_simulation.sh` can be used for end-to-end testing scenarios. The `throughput_eval.sh` script measures system performance under load. The `test_endpoints.sh` script sends http requests to each endpoint and checks all endpoints perform as expected.

## Code Quality & Style

This project enforces consistent code style and quality through automated tools.

### Code Formatting
The project uses **clang-format** with a custom configuration based on Google style:

```bash
# Format all C++ files
./scripts/format.sh

# Check if files need formatting (CI mode)
./scripts/format.sh --check

# Preview formatting changes without modifying files
./scripts/format.sh --dry-run

# Verbose output
./scripts/format.sh --verbose
```

**CMake Targets:**
```bash
make format        # Format all source files
make format-check  # Check formatting without changes
```

### Static Analysis
The project uses **clang-tidy** for static code analysis:

```bash
# Run static analysis
./scripts/lint.sh

# Run analysis with auto-fix
./scripts/lint.sh --fix

# Use multiple parallel jobs
./scripts/lint.sh --jobs 8

# Verbose output
./scripts/lint.sh --verbose
```

**CMake Targets:**
```bash
make lint          # Run static analysis
make lint-fix      # Run analysis with auto-fix
make quality       # Run both formatting check and linting
```

### Pre-commit Hooks
Set up automatic code quality checks before each commit:

```bash
# Install the pre-commit hook
ln -s ../../scripts/pre-commit.sh .git/hooks/pre-commit

# The hook will automatically run:
# 1. Code formatting check
# 2. Static analysis on staged files
# 3. Build verification
# 4. Common issue detection (TODOs, debug prints)
```

### Development Workflow Scripts
The project provides several scripts to automate common development tasks:

**Code Quality Scripts:**
```bash
./scripts/format.sh         # Format all source code
./scripts/lint.sh           # Run static analysis
./scripts/pre-commit.sh     # Pre-commit quality checks
```

**Build and Test Scripts:**
```bash
./scripts/build.sh          # Full project build
./scripts/run_tests.sh      # Execute all tests
./scripts/run_simulation.sh # Run trading simulation
./scripts/throughput_eval.sh # Performance benchmarking
```

All scripts are designed to be run from the project root directory and include proper error handling and colored output for better developer experience.

### Style Guide Summary
- **Indentation:** 4 spaces, no tabs
- **Line Length:** 100 characters
- **Braces:** Attached style (`{` on same line)
- **Naming:**
  - Classes/Structs: `CamelCase`
  - Functions: `camelBack`
  - Variables: `lower_case`
  - Private members: `lower_case_` (trailing underscore)
  - Constants: `UPPER_CASE`
- **Includes:** Sorted with project headers first
- **Namespace Usage:** 
  - Never use `using namespace std`
  - Always use explicit `std::` prefixes
  - Never add custom code to `std` namespace
  - Template specializations for std types go at global scope

### Configuration Files
- **`.clang-format`** - Code formatting rules (C++23 compatible)
- **`.clang-tidy`** - Static analysis configuration with C++23 checks
- Both files are tuned for financial software requirements

## Pull Request Guidelines

1. **Branch Naming:** Use descriptive branch names like `feature/order-matching` or `fix/memory-leak`
2. **Commit Messages:** Use conventional commit format: `type(scope): description`
3. **Testing:** Ensure all tests pass and add new tests for new features
4. **Code Review:** All PRs require at least one approval
5. **CI/CD:** All checks must pass before merging

## Testing

### Unit Tests
```bash
# Run all unit tests
./build/tests/unit_tests

# Run specific test suite
./build/tests/unit_tests --gtest_filter="OrderBookTest*"
```

### Integration Tests
```bash
# Run integration tests
./build/tests/integration_tests
```

### Performance Testing
```bash
# Run throughput evaluation
./scripts/throughput_eval.sh

# Run trading simulation (end-to-end testing)
./scripts/run_simulation.sh
```

The `throughput_eval.sh` script measures the system's ability to handle high-frequency trading scenarios, while `run_simulation.sh` provides end-to-end testing of the trading pipeline with realistic market data.

## Architecture Guidelines

- **Single Responsibility:** Each class should have one clear purpose
- **Dependency Injection:** Use shared_ptr for dependencies
- **Error Handling:** Use exceptions for exceptional cases, return codes for expected failures
- **Thread Safety:** Document thread safety guarantees for all public APIs
- **Performance:** Profile before optimizing, measure twice, cut once

cmake_minimum_required(VERSION 3.16)
project(live-trading-engine VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
enable_testing()

# Find packages
find_package(Threads REQUIRED)

# Include FetchContent for Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(googletest)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Use libc++ with Clang for better C++23 support
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

# Create trading engine library
file(GLOB_RECURSE TRADING_SOURCES "src/*.cpp")
add_library(trading_engine ${TRADING_SOURCES})
target_include_directories(trading_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(trading_engine Threads::Threads)

# Add subdirectories
add_subdirectory(apps)
add_subdirectory(tests)

# ==============================================================================
# Code Quality Targets
# ==============================================================================

# Find clang-format
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    # Get all source files
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/include/*.hpp
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/apps/*.cpp
        ${CMAKE_SOURCE_DIR}/tests/*.cpp
    )
    
    # Format target
    add_custom_target(format
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/format.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )
    
    # Format check target
    add_custom_target(format-check
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/format.sh --check
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking code formatting"
        VERBATIM
    )
    
    message(STATUS "clang-format found: ${CLANG_FORMAT}")
    message(STATUS "Added targets: format, format-check")
else()
    message(WARNING "clang-format not found. Code formatting targets not available.")
endif()

# Find clang-tidy
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    # Lint target
    add_custom_target(lint
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/lint.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running static analysis with clang-tidy"
        VERBATIM
    )
    
    # Lint fix target
    add_custom_target(lint-fix
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/lint.sh --fix
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running static analysis with auto-fix"
        VERBATIM
    )
    
    message(STATUS "clang-tidy found: ${CLANG_TIDY}")
    message(STATUS "Added targets: lint, lint-fix")
else()
    message(WARNING "clang-tidy not found. Static analysis targets not available.")
endif()

# Combined quality target
add_custom_target(quality
    DEPENDS format-check lint
    COMMENT "Running all code quality checks"
) 
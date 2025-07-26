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
find_package(PkgConfig QUIET)

# Try to find librdkafka first
if(PkgConfig_FOUND)
    pkg_check_modules(RdKafka QUIET rdkafka rdkafka++)
endif()

# Include FetchContent for dependencies
include(FetchContent)

# Google Test
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)

# librdkafka setup
if(RdKafka_FOUND)
    message(STATUS "Found system librdkafka: ${RdKafka_VERSION}")
    
    # Create imported targets for system librdkafka
    if(NOT TARGET RdKafka::rdkafka)
        add_library(RdKafka::rdkafka INTERFACE IMPORTED)
        target_link_libraries(RdKafka::rdkafka INTERFACE ${RdKafka_LIBRARIES})
        target_include_directories(RdKafka::rdkafka INTERFACE ${RdKafka_INCLUDE_DIRS})
        target_compile_options(RdKafka::rdkafka INTERFACE ${RdKafka_CFLAGS_OTHER})
    endif()
    
    if(NOT TARGET RdKafka::rdkafka++)
        add_library(RdKafka::rdkafka++ INTERFACE IMPORTED)
        target_link_libraries(RdKafka::rdkafka++ INTERFACE ${RdKafka_LIBRARIES})
        target_include_directories(RdKafka::rdkafka++ INTERFACE ${RdKafka_INCLUDE_DIRS})
        target_compile_options(RdKafka::rdkafka++ INTERFACE ${RdKafka_CFLAGS_OTHER})
    endif()
else()
    message(STATUS "librdkafka not found system-wide, downloading...")
    FetchContent_Declare(
      librdkafka
      GIT_REPOSITORY https://github.com/confluentinc/librdkafka.git
      GIT_TAG v2.3.0
      GIT_SHALLOW TRUE
    )
    
    # Configure librdkafka options
    set(RDKAFKA_BUILD_STATIC ON CACHE BOOL "Build static library")
    set(RDKAFKA_BUILD_SHARED OFF CACHE BOOL "Build shared library") 
    set(RDKAFKA_BUILD_TESTS OFF CACHE BOOL "Build tests")
    set(RDKAFKA_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
    set(WITH_SASL OFF CACHE BOOL "Enable SASL support")
    set(WITH_SSL ON CACHE BOOL "Enable SSL support")
    set(WITH_ZLIB ON CACHE BOOL "Enable zlib support")
    set(WITH_ZSTD ON CACHE BOOL "Enable zstd support")
    
    FetchContent_MakeAvailable(librdkafka)
    
    # Create aliases for consistency
    add_library(RdKafka::rdkafka ALIAS rdkafka)
    add_library(RdKafka::rdkafka++ ALIAS rdkafka++)
endif()

FetchContent_MakeAvailable(googletest)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Use libc++ with Clang for better C++23 support
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Only use libc++ on macOS to avoid ABI issues with system libraries on Linux
    if(APPLE)
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++)
    endif()
endif()

# Create trading engine library
file(GLOB_RECURSE TRADING_SOURCES "src/*.cpp")
add_library(trading_engine ${TRADING_SOURCES})
target_include_directories(trading_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Link libraries - make librdkafka PUBLIC so headers propagate to executables
target_link_libraries(trading_engine 
    PUBLIC 
        Threads::Threads
        RdKafka::rdkafka++
        RdKafka::rdkafka
)

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
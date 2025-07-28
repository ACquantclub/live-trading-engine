cmake_minimum_required(VERSION 3.20.0)
project(csp VERSION "0.11.1" LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake Policies
cmake_policy(SET CMP0077 NEW)  # option() behavior for variables
cmake_policy(SET CMP0074 NEW)  # find_package uses <PackageName>_ROOT variables
cmake_policy(SET CMP0094 NEW)  # Find the first version that matches constraints

# Environment detection
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(WIN32 ON)
    set(MACOS OFF)
    set(LINUX OFF)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(WIN32 OFF)
    set(MACOS ON)
    set(LINUX OFF)
else()
    set(WIN32 OFF)
    set(MACOS OFF)
    set(LINUX ON)
endif()

# Build options
option(CMAKE_BUILD_TYPE "Release/Debug build" RELEASE)
option(CSP_BUILD_NO_CXX_ABI "Do not use CXX11 ABI" OFF)

# Find required packages
find_package(Threads REQUIRED)

# RapidJSON - try to find system package first
find_package(RapidJSON QUIET)
if(RapidJSON_FOUND)
    message(STATUS "Found system RapidJSON")
else()
    # Look for headers manually
    find_path(RapidJSON_INCLUDE_DIRS rapidjson/rapidjson.h
        PATHS /usr/include /usr/local/include)
    if(RapidJSON_INCLUDE_DIRS)
        message(STATUS "Found RapidJSON headers at: ${RapidJSON_INCLUDE_DIRS}")
    else()
        message(FATAL_ERROR "RapidJSON not found. Please install rapidjson-dev package.")
    endif()
endif()

# Compiler flags
if(WIN32)
    if(CMAKE_BUILD_TYPE_LOWER STREQUAL debug)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DEBUG /Z7 /Zi")
        add_definitions(-DCSP_DEBUG)
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2")
        add_definitions(-DNDEBUG)
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /MP /bigobj")
    foreach(warning 4244 4251 4267 4275 4290 4786 4305 4996)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd${warning}")
    endforeach()
    add_compile_definitions(WIN32 _WIN32)
else()
    if(CSP_BUILD_NO_CXX_ABI)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")
    endif()
    
    string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWER)
    if(CMAKE_BUILD_TYPE_LOWER STREQUAL debug)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g3")
        add_definitions(-DCSP_DEBUG)
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -Wall -Wno-deprecated-declarations -Wno-deprecated -Wno-missing-field-initializers -Wno-unused-parameter")
        add_definitions(-DNDEBUG)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
        endif()
    endif()
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CSP_RUNTIME_INSTALL_SUBDIR lib/)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CSP_RUNTIME_INSTALL_SUBDIR bin/)
endif()

# Include directories
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/cpp")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/cpp")
if(RapidJSON_INCLUDE_DIRS)
    include_directories(${RapidJSON_INCLUDE_DIRS})
endif()

# Add components
add_subdirectory(cpp/csp/core)
add_subdirectory(cpp/csp/engine)

# VGMTrans (c) 2002-2026
# Licensed under the zlib license
# Check the included LICENSE.txt for details

# macOS Apple Silicon (arm64) toolchain file
# This defines the base flags for building on Apple Silicon Macs

# Set these only when actually cross-compiling or
# CMake will think you are cross-compiling. 
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_SYSTEM_NAME Darwin)
endif()
if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|AARCH64")
    set(CMAKE_SYSTEM_PROCESSOR arm64)
endif()

# Force the architecture and deployment target.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS Apple Silicon Architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS Minimum OS Version")

# Base flags shared by both C and C++ compilers
set(BASE_C_FLAGS
    -Wall               # Enable most standard compiler warnings
    -Wextra             # Enable additional extra warnings
    -Wshadow            # Warn when a local variable shadows another variable
    -Wunused            # Warn about unused variables, parameters, and functions
    -Wcast-align        # Warn when a pointer cast increases alignment requirements
    -Wnull-dereference  # Warn if a potential null pointer dereference is detected
    -fvisibility=hidden # Hide internal symbols by default (reduces binary size/link time)
)

# C++ specific flags
set(BASE_CXX_FLAGS
    ${BASE_C_FLAGS}
    -Wold-style-cast     # Warn when using C-style casts instead of C++ casts
    -Woverloaded-virtual # Warn if a derived class hides base class virtual functions
)

string(JOIN " " BASE_C_FLAGS_STR ${BASE_C_FLAGS})
string(JOIN " " BASE_CXX_FLAGS_STR ${BASE_CXX_FLAGS})

set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${BASE_C_FLAGS_STR}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${BASE_CXX_FLAGS_STR}")

set(CMAKE_C_FLAGS_DEBUG_INIT "${CMAKE_C_FLAGS_INIT} -O0 -g")
set(CMAKE_C_FLAGS_RELEASE_INIT "${CMAKE_C_FLAGS_INIT} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "${CMAKE_CXX_FLAGS_INIT} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "${CMAKE_CXX_FLAGS_INIT} -O3 -DNDEBUG")

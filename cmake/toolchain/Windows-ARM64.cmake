# VGMTrans (c) 2002-2026
# Licensed under the zlib license
# Check the included LICENSE.txt for details

# Windows ARM64 toolchain file for MSVC and clang-cl
# This defines the base flags for building on Windows ARM64
# and is used by the presets in CMakePresets.json

# Set these only when actually cross-compiling or
# CMake will think you are cross-compiling. 
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_SYSTEM_NAME Windows)
endif()
if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64|AARCH64")
    set(CMAKE_SYSTEM_PROCESSOR ARM64)
endif()

# Refer to MSVC documentation for more information on these flags,
# we provide a brief reason for why they're being turned on below.

set(BASE_CFLAGS
    # Preprocessor Definitions
    /DNOMINMAX                 # Avoids macro collisions with std::min/max functions  
    /D_CRT_SECURE_NO_WARNINGS  # Allow use of "unsafe" functions (e.g. sprintf)
    /D_CRT_NONSTDC_NO_WARNINGS # Allow use of non-standard functions (e.g. strdup)
    /D_USE_MATH_DEFINES        # Allow use of math constants (e.g. M_PI)
    /DUNICODE                  # Use Unicode character set
    /D_UNICODE                 # Use Unicode character set

    # Compiler Flags
    /permissive-      # Put MSVC into "good compiler mode"
    /Zc:__cplusplus   # Report the correct C++ standard in the __cplusplus macro
    /volatile:iso     # Use ISO C++ volatile semantics (Crucial for ARM64 performance)
    /utf-8            # Set source and execution character sets to UTF-8
    /W3               # Warning level 3
)

# Safely join the list into a single space-separated string
string(JOIN " " BASE_CFLAGS_STR ${BASE_CFLAGS})

# Append to _INIT to preserve CMake's default internal platform flags (like /EHsc and /GR)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${BASE_CFLAGS_STR}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${BASE_CFLAGS_STR}")

set(CMAKE_C_FLAGS_DEBUG_INIT "${CMAKE_C_FLAGS_INIT} /Zi /Od /RTC1")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "${CMAKE_CXX_FLAGS_INIT} /Zi /Od /RTC1")

set(CMAKE_C_FLAGS_RELEASE_INIT "${CMAKE_C_FLAGS_INIT} /O2 /Ob2 /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "${CMAKE_CXX_FLAGS_INIT} /O2 /Ob2 /DNDEBUG")
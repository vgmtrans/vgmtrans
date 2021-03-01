# VGMTrans (c) 2002-2021
# Licensed under the zlib license
# Check the included LICENSE.txt for details

# For object library liinking
cmake_minimum_required(VERSION 3.12)

# People still use Yosemite
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12.0" CACHE STRING "Minimum OSX version" FORCE)

project(VGMTrans VERSION 0.1 LANGUAGES CXX)

add_library(g_options INTERFACE)
add_library(g_warnings INTERFACE)

# Default to Release if build type wasn't specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
            "Release"
            CACHE STRING "Build type (Release/Debug/RelWithDebInfo/MinSizeRel)" FORCE)
endif()

message(STATUS "Will build in ${CMAKE_BUILD_TYPE} mode")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

#
# Options
#

option(ENABLE_ASAN "Enable address sanitizer" OFF)
option(ENABLE_UI_QT "Build the UI (Qt)" ON)
option(ENABLE_LIBCPP "Build using libc++" OFF)

#
# Compiler and build settings
#

# Use ccache if present on the system
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif()

# Defaulting to C++17
target_compile_features(g_options INTERFACE cxx_std_17)

if(MSVC)
    target_compile_options(g_options
            INTERFACE
            -DNOMINMAX
            -D_CRT_SECURE_NO_DEPRECATE
            -D_CRT_NONSTDC_NO_DEPRECATE
            -D_CRT_SECURE_NO_WARNINGS
            /permissive-
            )

    target_compile_options(g_warnings
            INTERFACE /W3)

else()
    if(ENABLE_ASAN)
        target_compile_options(g_options INTERFACE -fsanitize=address)
        target_link_libraries(g_options INTERFACE -fsanitize=address)
    endif()

    if(NOT ${CMAKE_BUILD_TYPE} MATCHES "Release")
        target_compile_options(g_warnings
                INTERFACE
                -Wall
                # -Werror turn on, someday
                -Wextra
                -Wshadow
                -Wunused
                -Wold-style-cast
                -Woverloaded-virtual
                -Wcast-align
                -Wnull-dereference)
    endif()

    # The c++17 filesystem library requires some thinkering with compile flags
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        target_link_libraries(g_options INTERFACE stdc++fs)
    else()
        if(ENABLE_LIBCPP)
            target_compile_options(g_options INTERFACE -stdlib=libc++)
        else()
            target_link_libraries(g_options INTERFACE stdc++fs)
        endif()
    endif()
endif()

# Include our own until we switch to TinyXML2 (should also be moved to vgmtranscore)
add_compile_definitions(TIXML_USE_STL)
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/tinyxml" "${CMAKE_CURRENT_BINARY_DIR}/tinyxml" EXCLUDE_FROM_ALL)

add_subdirectory(src)

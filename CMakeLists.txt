# VGMTrans (c) 2002-2026
# Licensed under the zlib license
# Check the included LICENSE.txt for details

# For cmake-presets v3
cmake_minimum_required(VERSION 3.21)

project(
  VGMTrans
  VERSION 1.3
  LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Git)
if(GIT_FOUND)
  execute_process(
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMAND ${GIT_EXECUTABLE} rev-parse --git-path HEAD
    OUTPUT_VARIABLE GIT_HEAD_FILEREF
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${GIT_HEAD_FILEREF}")

  execute_process(
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    OUTPUT_VARIABLE VCS_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  execute_process(
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE VCS_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  execute_process(
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --dirty=+
    OUTPUT_VARIABLE VCS_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(VCS_TAG "v${CMAKE_PROJECT_VERSION}")
  set(VCS_HASH "unknown")
  set(VCS_BRANCH "unknown")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ~~~
# Options
# ~~~

option(ENABLE_UI_QT "Build the UI (Qt)" ON)
option(ENABLE_CLI "Build the CLI application" ON)
option(ENABLE_SHELL "Build the shell application" ON)

# ~~~
# Dependencies
# ~~~

add_subdirectory("${PROJECT_SOURCE_DIR}/lib/fmt")
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library")
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/spdlog")
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/mio")

# ~~~
# Source
# ~~~

add_subdirectory(src)

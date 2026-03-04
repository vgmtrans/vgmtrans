# VGMTrans (c) 2002-2026
# Licensed under the zlib license
# Check the included LICENSE.txt for details

cmake_minimum_required(VERSION 3.23)

project(
  VGMTrans
  VERSION 1.3
  LANGUAGES C CXX)

include(GNUInstallDirs)

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
    COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    OUTPUT_VARIABLE VCS_HASH_COMPLETE
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
  set(VCS_HASH "unknown")
  set(VCS_BRANCH "unknown")
endif()

# Fallback for VCS_TAG if git failed or returned empty
if(NOT VCS_TAG)
  set(VCS_TAG "v${CMAKE_PROJECT_VERSION}")
endif()

string(TIMESTAMP VCS_DATE "%Y-%m-%d")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ~~~
# Options
# ~~~

option(ENABLE_UI_QT "Build the UI (Qt)" ON)
option(ENABLE_CLI "Build the CLI application" ON)
option(ENABLE_SHELL "Build the shell application" ON)

option(NO_QT_DEPLOY "Don't run the Qt deploy script during install (useful for system/Flatpak packaging)" OFF)

option(USE_SYSTEM_FMT "Use system-provided fmt library" OFF)
option(USE_SYSTEM_SPDLOG "Use system-provided spdlog library" OFF)
option(USE_SYSTEM_ZLIB "Use system-provided zlib library" OFF)
option(USE_SYSTEM_NLOHMANN "Use system-provided nlohmann_json library" OFF)
option(USE_SYSTEM_UNARR "Use system-provided unarr library" OFF)
option(USE_SYSTEM_LIBCHDR "Use system-provided libchdr library" OFF)
option(USE_SYSTEM_MINIZIP "Use system-provided minizip library" OFF)

# ~~~
# Source
# ~~~

add_subdirectory(src)

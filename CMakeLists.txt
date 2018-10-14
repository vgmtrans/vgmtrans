# VGMTrans (c) 2018
# Licensed under the zlib license
# Check the included LICENSE.txt for details
cmake_minimum_required(VERSION 3.5)

project(VGMTrans)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Environment settings #

set(CMAKE_OSX_ARCHITECTURES "x86_64")
# Clang compilation database, needed for IWYU
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 14)
# Make sure the compiler supports C++14
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
        CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
    message(FATAL_ERROR "GCC >= 5.0 required (found \
                        ${CMAKE_CXX_COMPILER_VERSION})")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
        CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.4)
    message(FATAL_ERROR "Clang >= 3.4 required (found \
                        ${CMAKE_CXX_COMPILER_VERSION})")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
        CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.10)
    message(FATAL_ERROR "MSVC >= 19.10 (VS2015) required (found \
                        ${CMAKE_CXX_COMPILER_VERSION})")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Set compiler-specific flags
if(MSVC)
    add_compile_options(/permissive-)
    add_compile_options(/Zc:inline)
    add_compile_options(/Zc:throwingNew)
    add_compile_options(/Zc:strictStrings-)
    add_compile_options(/volatile:iso)
    add_compile_options(/MP)

    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
    add_definitions(-DVGMTRANS_FOR_QT) #TODO: Remove

    string(APPEND CMAKE_EXE_LINKER_FLAGS " /NXCOMPAT")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -Wtype-limits \
  -Wsign-compare -Wuninitialized")
    set(CMAKE_CXX_FLAGS_RELEASE "-w -Os -fomit-frame-pointer")

    # Turns out that "-s" in Apple's clang is not really for stripping
    if(NOT APPLE)
        string(APPEND CMAKE_CXX_FLAGS_RELEASE " -s")
    endif()
endif()

# Dependencies needed for the backend #

find_package(ZLIB) # Needed for psf decompression
if(ZLIB_FOUND)
    message(STATUS "Using system Zlib ${ZLIB_VERSION_STRING}")
else()
    message(STATUS "Using static Zlib fallback")
    add_subdirectory(lib/zlib)
endif ()

find_package(MINIZIP) # Needed for the MAME loader
if(MINIZIP_FOUND)
    message(STATUS "Using system MiniZip")
else()
    message(STATUS "Using static MiniZip fallback")
    add_subdirectory(lib/zlib/minizip)
endif()

find_package(TINYXML)
if(TINYXML_FOUND)
    message(STATUS "Using system TinyXML")
else()
    message(STATUS "Using static TinyXML fallback") # Needed for the MAME loader
    add_subdirectory(lib/tinyxml)
endif()
add_definitions(-DTIXML_USE_STL)

# Qt frontend dependencies #

find_package(Qt5 REQUIRED COMPONENTS Gui Widgets)
if(Qt5Core_FOUND)
    message(STATUS "Using system Qt ${Qt5Core_VERSION}")
else()
    message(FATAL_ERROR "Please install Qt5! Static fallback not available")
endif()

set(CMAKE_AUTOMOC ON)
set(RESOURCE_PATH
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/resources.qrc"
        )
qt5_add_resources(UI_RESOURCES ${RESOURCE_PATH})

find_package(FLUIDSYNTH REQUIRED)
if(FLUIDSYNTH_FOUND)
    message(STATUS "Using system FluidSynth ${FLUIDSYNTH_VERSION_STRING}")
else()
    message(FATAL_ERROR "Please install FluidSynth! Static fallback not available")
endif()


# Targets #

file(GLOB_RECURSE MAIN_FILES
        "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/main/*.h")

file(GLOB UI_FILES
        "${PROJECT_SOURCE_DIR}/src/ui/qt/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui/qt/*.h")

set(INCLUDE_DIRS
        ${PROJECT_SOURCE_DIR}/src/main
        ${PROJECT_SOURCE_DIR}/src/main/formats
        ${PROJECT_SOURCE_DIR}/src/main/loaders
        ${PROJECT_SOURCE_DIR}/src/ui/qt
        )

add_executable(
        vgmtrans ${MAIN_FILES} ${UI_FILES} ${UI_RESOURCES})

target_include_directories(vgmtrans
        PRIVATE
        ${INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Qt5Gui_PRIVATE_INCLUDE_DIRS}
        )

target_link_libraries(vgmtrans
        PRIVATE
        ZLIB::ZLIB
        minizip
        tinyxml
        Qt5::Widgets
        fluidsynth
)

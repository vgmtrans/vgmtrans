cmake_minimum_required(VERSION 2.8.11)
project(VGMTrans)

if(APPLE)
    set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-apple-darwin11")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9")
    set(GUI_TYPE MACOSX_BUNDLE)
endif(APPLE)

if(WIN32)
    set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-w64-mingw32")
    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(GUI_TYPE WIN32)
endif(WIN32)

set ( CMAKE_CXX_FLAGS "-std=c++11 -w -g3 -Wall -O0" )

set(CMAKE_PREFIX_PATH
        ${CMAKE_PREFIX_PATH}
        "${CMAKE_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}")
message(STATUS "PREFIX PATH: " ${CMAKE_PREFIX_PATH})

set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        ${CMAKE_SOURCE_DIR}/cmake)

file(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp" "${PROJECT_SOURCE_DIR}/src/main/*.h")
file(GLOB UI_FILES "${PROJECT_SOURCE_DIR}/src/ui/qt/*.cpp" "${PROJECT_SOURCE_DIR}/src/ui/qt/*.h")
file(GLOB_RECURSE LIB_FILES
        "${PROJECT_SOURCE_DIR}/lib/fluidsynth/mem_sfloader/*.c" "${PROJECT_SOURCE_DIR}/lib/fluidsynth/mem_sfloader/*.h"
        "${PROJECT_SOURCE_DIR}/lib/tinyxml/*.cpp" "${PROJECT_SOURCE_DIR}/lib/fluidsynth/*.h"
        "${PROJECT_SOURCE_DIR}/lib/minizip/*.c" "${PROJECT_SOURCE_DIR}/lib/minizip/*.h"
        )

set(INCLUDE_DIRS
        ${PROJECT_SOURCE_DIR}/src/main
        ${PROJECT_SOURCE_DIR}/src/main/formats
        ${PROJECT_SOURCE_DIR}/src/main/loaders
        ${PROJECT_SOURCE_DIR}/src/ui/qt
        ${PROJECT_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include
        ${PROJECT_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include/glib-2.0
        ${PROJECT_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include/fluidsynth
        ${PROJECT_SOURCE_DIR}/lib/zlib/include
        ${PROJECT_SOURCE_DIR}/lib/tinyxml
        ${PROJECT_SOURCE_DIR}/lib/fluidsynth/mem_sfloader

        )

set(RESOURCE_PATH
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/resources.qrc"
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/qdarkstyle/style.qrc")

source_group("ui" REGULAR_EXPRESSION .*src/ui/qt/.*)
source_group("main\\formats" REGULAR_EXPRESSION .*src/main/formats/.*)
source_group("main\\loaders" REGULAR_EXPRESSION .*src/main/loaders/.*)
source_group("main" REGULAR_EXPRESSION .*src/main/.*)
source_group("lib\\fluidsynth" REGULAR_EXPRESSION .*lib/fluidsynth/.*)
source_group("lib\\tinyxml" REGULAR_EXPRESSION .*lib/tinyxml/.*)
source_group("lib\\minizip" REGULAR_EXPRESSION .*lib/minizip/.*)

include_directories(${INCLUDE_DIRS})


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
## We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
add_definitions(${Qt5Widgets_DEFINITIONS})
# Find the QtWidgets library
find_package(Qt5Widgets REQUIRED)

qt5_add_resources(UI_RESOURCES ${RESOURCE_PATH})

add_executable(vgmtrans ${GUI_TYPE} ${MAIN_FILES} ${UI_FILES} ${LIB_FILES} ${UI_RESOURCES})

# Use the Widgets module from Qt 5.
target_link_libraries(vgmtrans Qt5::Core Qt5::Widgets)

if (WIN32 OR APPLE)
    set_target_properties(vgmtrans PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(vgmtrans PROPERTIES LINK_SEARCH_END_STATIC 1)
endif (WIN32 OR APPLE)

find_package (ZLIB REQUIRED)
if (ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIRS})
    target_link_libraries (vgmtrans ${ZLIB_LIBRARIES})
endif (ZLIB_FOUND)

find_package (FluidSynth REQUIRED)
target_link_libraries(vgmtrans ${FLUIDSYNTH_LIBRARIES})

find_package(GLIB REQUIRED)
target_link_libraries(vgmtrans ${GLIB_LIBRARIES})

if (WIN32 OR APPLE)
    find_library(QTPCRE_LIBRARY qtpcre)
    target_link_libraries(vgmtrans ${QTPCRE_LIBRARY})

    find_library(INTL_LIBRARY NAMES intl)
    target_link_libraries(vgmtrans ${INTL_LIBRARY})

    find_library(ICONV_LIBRARIES NAMES iconv)
    target_link_libraries(vgmtrans ${ICONV_LIBRARIES})
endif (WIN32 OR APPLE)

add_definitions(-DIOAPI_NO_64)
add_definitions(-DTIXML_USE_STL)

if (WIN32)
    set_property(TARGET Qt5::Core APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32)
    find_library(QWINDOWS_LIBRARY qwindows)
    target_link_libraries(vgmtrans
            ${QWINDOWS_LIBRARY}

            imm32.lib
            winmm.lib
            dsound.lib)

    add_definitions(-DFLUIDSYNTH_NOT_A_DLL -DG_OS_WIN32)

endif (WIN32)

if (APPLE)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(APPKIT_LIBRARY AppKit)
    find_library(CARBON_LIBRARY Carbon)
    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(CORESERVICES_LIBRARY CoreServices)

    find_library(QCOCOA_LIBRARY qcocoa)
    find_library(QT5PLATFORMSUPPORT_LIBRARY Qt5PlatformSupport)
    find_library(QT5PRINTSUPPORT_LIBRARY Qt5PrintSupport)
    find_library(CUPS_LIBRARY cups)

    target_link_libraries(vgmtrans
            ${QCOCOA_LIBRARY}
            ${APPKIT_LIBRARY}
            ${CARBON_LIBRARY}
            ${COREMIDI_LIBRARY}
            ${COREAUDIO_LIBRARY}
            ${AUDIOUNIT_LIBRARY}
            ${COREFOUNDATION_LIBRARY}
            ${CORESERVICES_LIBRARY}

            ${QT5PLATFORMSUPPORT_LIBRARY}
            ${QT5PRINTSUPPORT_LIBRARY}
            ${CUPS_LIBRARY}
            ${IOKIT_LIBRARY}

            )

endif (APPLE)

cmake_minimum_required(VERSION 2.8.11)
project(VGMTrans)

#SET(PROJECT_SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

IF(APPLE)
    SET(CMAKE_LIBRARY_ARCHITECTURE "x86_64-apple-darwin11")
    SET(CMAKE_OSX_ARCHITECTURES "x86_64;")
    SET(CMAKE_OSX_DEPLOYMENT_TARGET "10.9")
    SET(GUI_TYPE MACOSX_BUNDLE)
ENDIF(APPLE)

IF(WIN32)
    SET(CMAKE_LIBRARY_ARCHITECTURE "i686-w64-mingw32")
    SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    SET(GUI_TYPE WIN32)
ENDIF(WIN32)

#set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR})

set(CMAKE_PREFIX_PATH
    ${CMAKE_PREFIX_PATH}
    "${CMAKE_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}")
message(STATUS "PREFIX PATH: " ${CMAKE_PREFIX_PATH})

set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${CMAKE_SOURCE_DIR}/cmake)


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
## We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
add_definitions(${Qt5Widgets_DEFINITIONS})
## Executables fail to build with Qt 5 in the default configuration
## without -fPIE. We add that here.
set(CMAKE_CXX_FLAGS "${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")

# Find the QtWidgets library
find_package(Qt5Widgets REQUIRED)

FILE(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp" "${PROJECT_SOURCE_DIR}/src/main/*.h")
FILE(GLOB UI_FILES "${PROJECT_SOURCE_DIR}/src/ui/qt/*.cpp" "${PROJECT_SOURCE_DIR}/src/ui/qt/*.h")
FILE(GLOB_RECURSE LIB_FILES
    "${PROJECT_SOURCE_DIR}/lib/fluidsynth/mem_sfloader/*.c" "${PROJECT_SOURCE_DIR}/lib/fluidsynth/mem_sfloader/*.h"
    "${PROJECT_SOURCE_DIR}/lib/tinyxml/*.cpp" "${PROJECT_SOURCE_DIR}/lib/fluidsynth/*.h"
    "${PROJECT_SOURCE_DIR}/lib/minizip/*.c" "${PROJECT_SOURCE_DIR}/lib/minizip/*.h"
    )

SET(INCLUDE_DIRS
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

SET(RESOURCE_PATH
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/resources.qrc"
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/qdarkstyle/style.qrc")

SOURCE_GROUP("ui" REGULAR_EXPRESSION .*src/ui/qt/.*)
SOURCE_GROUP("main\\formats" REGULAR_EXPRESSION .*src/main/formats/.*)
SOURCE_GROUP("main\\loaders" REGULAR_EXPRESSION .*src/main/loaders/.*)
SOURCE_GROUP("main" REGULAR_EXPRESSION .*src/main/.*)
SOURCE_GROUP("lib\\fluidsynth" REGULAR_EXPRESSION .*lib/fluidsynth/.*)
SOURCE_GROUP("lib\\tinyxml" REGULAR_EXPRESSION .*lib/tinyxml/.*)
SOURCE_GROUP("lib\\minizip" REGULAR_EXPRESSION .*lib/minizip/.*)




#MESSAGE(STATUS "QT FILES" ${QT_FILES})
#source_group("ui" FILES ${QT_FILES})

set ( CMAKE_CXX_FLAGS "-std=c++11 -w -g3 -Wall -O0" )

include_directories(${INCLUDE_DIRS})

qt5_add_resources(UI_RESOURCES ${RESOURCE_PATH})

add_executable(vgmtrans ${GUI_TYPE} ${MAIN_FILES} ${UI_FILES} ${LIB_FILES} ${UI_RESOURCES})

#target_link_libraries(vgmtrans main)
#target_link_libraries(vgmtrans ui)

IF (WIN32 OR APPLE)
    SET_TARGET_PROPERTIES(vgmtrans PROPERTIES LINK_SEARCH_START_STATIC 1)
    SET_TARGET_PROPERTIES(vgmtrans PROPERTIES LINK_SEARCH_END_STATIC 1)
ENDIF (WIN32 OR APPLE)

# Use the Widgets module from Qt 5.
target_link_libraries(vgmtrans Qt5::Widgets)

find_package (ZLIB REQUIRED)
if (ZLIB_FOUND)
  include_directories(${ZLIB_INCLUDE_DIRS})
  target_link_libraries (vgmtrans ${ZLIB_LIBRARIES})
endif (ZLIB_FOUND)

find_package (FluidSynth REQUIRED)
target_link_libraries(vgmtrans ${FLUIDSYNTH_LIBRARIES})

find_package(GLIB REQUIRED)
target_link_libraries(vgmtrans ${GLIB_LIBRARIES})

IF (WIN32 OR APPLE)
    FIND_LIBRARY(INTL_LIBRARY NAMES intl)
    target_link_libraries(vgmtrans ${INTL_LIBRARY})

    FIND_LIBRARY(ICONV_LIBRARIES NAMES iconv)
    target_link_libraries(vgmtrans ${ICONV_LIBRARIES})
ENDIF (WIN32 OR APPLE)

add_definitions(-DIOAPI_NO_64)
add_definitions(-DTIXML_USE_STL)

IF (WIN32)
    SET_PROPERTY(TARGET Qt5::Core APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32)
    FIND_LIBRARY(QWINDOWS_LIBRARY qwindows)
    TARGET_LINK_LIBRARIES(vgmtrans
     ${QWINDOWS_LIBRARY}

     imm32.lib
     winmm.lib
     dsound.lib)

    ADD_DEFINITIONS(-DFLUIDSYNTH_NOT_A_DLL -DG_OS_WIN32)

ENDIF (WIN32)

IF (APPLE)
   FIND_LIBRARY(IOKIT_LIBRARY IOKit)
   FIND_LIBRARY(APPKIT_LIBRARY AppKit)
   FIND_LIBRARY(CARBON_LIBRARY Carbon)
   FIND_LIBRARY(COREMIDI_LIBRARY CoreMIDI)
   FIND_LIBRARY(COREAUDIO_LIBRARY CoreAudio)
   FIND_LIBRARY(AUDIOUNIT_LIBRARY AudioUnit)
   FIND_LIBRARY(COREFOUNDATION_LIBRARY CoreFoundation)
   FIND_LIBRARY(CORESERVICES_LIBRARY CoreServices)

   FIND_LIBRARY(QCOCOA_LIBRARY qcocoa)
   FIND_LIBRARY(QT5PLATFORMSUPPORT_LIBRARY Qt5PlatformSupport)
   FIND_LIBRARY(QT5PRINTSUPPORT_LIBRARY Qt5PrintSupport)
   FIND_LIBRARY(CUPS_LIBRARY cups)

   TARGET_LINK_LIBRARIES(vgmtrans
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

ENDIF (APPLE)

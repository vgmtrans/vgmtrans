cmake_minimum_required(VERSION 2.8.11)
project(VGMTrans)

IF(APPLE)
    SET(CMAKE_LIBRARY_ARCHITECTURE "x86_64-apple-darwin11")
    SET(CMAKE_OSX_ARCHITECTURES "x86_64;")
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

add_subdirectory(src)
add_subdirectory(lib)
add_subdirectory(contrib)


if(MSVC)
  set(CMAKE_CXX_FLAGS "/EHsc")
else()
  set ( CMAKE_CXX_FLAGS "-std=c++11 -w -g3 -Wall -O0" )
endif()

include_directories(${INCLUDE_DIRS})

add_executable(vgmtrans ${GUI_TYPE} ${SOURCE_FILES} ${UI_RESOURCES})

SET_TARGET_PROPERTIES(vgmtrans PROPERTIES LINK_SEARCH_START_STATIC 1)
SET_TARGET_PROPERTIES(vgmtrans PROPERTIES LINK_SEARCH_END_STATIC 1)

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

add_definitions(-DIOAPI_NO_64)
add_definitions(-DTIXML_USE_STL)

IF (WIN32 OR APPLE)
    FIND_LIBRARY(INTL_LIBRARY NAMES intl)
    target_link_libraries(vgmtrans ${INTL_LIBRARY})

    FIND_LIBRARY(ICONV_LIBRARIES NAMES iconv)
    target_link_libraries(vgmtrans ${ICONV_LIBRARIES})
ENDIF (WIN32 OR APPLE)

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

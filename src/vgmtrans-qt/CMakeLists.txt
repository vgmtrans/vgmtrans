if(APPLE AND EXISTS /usr/local/lib/)
  # Generic Homebrew include path
  list(APPEND CMAKE_PREFIX_PATH "/usr/local/include")
endif()

# TODO: Make optional: no fluidsynth, no playback support.
find_package(FLUIDSYNTH)
if(FLUIDSYNTH_FOUND)
  message(STATUS "Using FluidSynth ${FLUIDSYNTH_VERSION}")
else()
  if(MSVC)
    add_library(fluidsynth STATIC IMPORTED)
    set_target_properties(
      fluidsynth
      PROPERTIES
        IMPORTED_LOCATION
        "${CMAKE_SOURCE_DIR}/externals/win-deps/fluidsynth2-msvc2019-x64/fluidsynth.lib"
      )

    target_include_directories(
      fluidsynth
      INTERFACE
        ${CMAKE_SOURCE_DIR}/externals/win-deps/fluidsynth2-msvc2017-x64/include)

    message(STATUS "Using FluidSynth fallback from Windows externals")
  else()
    message(
      FATAL_ERROR "Please install FluidSynth! Bundled version not available")
  endif()
endif()

if(APPLE AND EXISTS /usr/local/opt/qt5)
  # Homebrew installs Qt5 (up to at least 5.9.1) in /usr/local/qt5, ensure it
  # can be found by CMake since it is not in the default /usr/local prefix.
  list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt5")
endif()

if(NOT Qt5_DIR AND MSVC)
  set(Qt5_DIR
      "${CMAKE_SOURCE_DIR}/externals/win-deps/qt5-msvc2019-x64/lib/cmake/Qt5")
  message(STATUS "Using Qt5 fallback")
endif()

find_package(Qt5 REQUIRED COMPONENTS Gui Widgets)
message(STATUS "Using Qt ${Qt5Core_VERSION}")

set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES "")
set(CMAKE_AUTOMOC ON)
qt5_add_resources(UI_RESOURCES
                  "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")

if(WIN32)
  set(GUI_TYPE WIN32)
elseif(APPLE)
  set(GUI_TYPE MACOSX_BUNDLE)
endif()

add_subdirectory(workarea/QHexView)

add_executable(vgmtrans
               ${GUI_TYPE}
               ${QHEXVIEW_HEADERS}
               ${QHEXVIEW_SOURCES}
               ${UI_RESOURCES}
               About.cpp
               About.h
               IconBar.cpp
               IconBar.h
               Logger.cpp
               Logger.h
               MainWindow.cpp
               MainWindow.h
               MenuBar.cpp
               MenuBar.h
               MusicPlayer.cpp
               MusicPlayer.h
               QtVGMRoot.cpp
               QtVGMRoot.h
               main_ui.cpp
               util/HeaderContainer.cpp
               util/HeaderContainer.h
               util/Helpers.cpp
               util/Helpers.h
               workarea/MdiArea.cpp
               workarea/MdiArea.h
               workarea/RawFileListView.cpp
               workarea/RawFileListView.h
               workarea/VGMCollListView.cpp
               workarea/VGMCollListView.h
               workarea/VGMCollView.cpp
               workarea/VGMCollView.h
               workarea/VGMFileTreeView.cpp
               workarea/VGMFileTreeView.h
               workarea/VGMFileView.cpp
               workarea/VGMFileView.h
               workarea/VGMFilesList.cpp
               workarea/VGMFilesList.h
               phantom/phantomstyle.cpp
               phantom/phantomcolor.cpp)

target_include_directories(vgmtrans PRIVATE . ./util ./workarea)

target_link_libraries(vgmtrans
                      PRIVATE g_options
                              g_warnings
                              vgmcore
                              Qt5::Widgets
                              fluidsynth)

if(APPLE)
  set(BUNDLE_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VGMTrans.app)

  set_target_properties(vgmtrans
                        PROPERTIES MACOSX_BUNDLE
                                   true
                                   MACOSX_BUNDLE_INFO_PLIST
                                   ${CMAKE_SOURCE_DIR}/data/Info.plist.in
                                   OUTPUT_NAME
                                   VGMTrans)

  target_sources(vgmtrans PRIVATE "${CMAKE_SOURCE_DIR}/data/VGMTrans.icns")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/data/VGMTrans.icns"
                              PROPERTIES
                              MACOSX_PACKAGE_LOCATION
                              Resources)
elseif(WIN32)
  target_sources(vgmtrans PRIVATE "${CMAKE_SOURCE_DIR}/data/VGMTrans.rc")
else()
  install(TARGETS vgmtrans DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()

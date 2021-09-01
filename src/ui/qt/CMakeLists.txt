add_subdirectory("${PROJECT_SOURCE_DIR}/lib/gsl-lite"
                 "${CMAKE_CURRENT_BINARY_DIR}/gsl-lite" EXCLUDE_FROM_ALL)

set(FluidSynth_FIND_VERSION "2.0.0")
find_package(FluidSynth)
if(FluidSynth_FOUND AND "${FluidSynth_VERSION}" MATCHES "2(.)*")
  message(STATUS "Using FluidSynth ${FluidSynth_VERSION}")
else()
  if(MSVC)

    message(STATUS Compiling for ${CMAKE_SYSTEM_PROCESSOR})
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64|amd64|AMD64")
      set(FLUID_LIBRARY_ARCH "x64")
    elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
      set(FLUID_LIBRARY_ARCH "arm64")
    endif()

    add_library(FluidSynth::FluidSynth SHARED IMPORTED)
    set_target_properties(
      FluidSynth::FluidSynth
      PROPERTIES
        IMPORTED_IMPLIB
        "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/solidsynth-msvc2019-${FLUID_LIBRARY_ARCH}/fluidsynth.lib"
    )

    set_target_properties(
      FluidSynth::FluidSynth
      PROPERTIES
        IMPORTED_LOCATION
        "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/solidsynth-msvc2019-${FLUID_LIBRARY_ARCH}/libfluidsynth-3.dll"
    )

    target_include_directories(
      FluidSynth::FluidSynth
      INTERFACE
        "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/solidsynth-msvc2019-${FLUID_LIBRARY_ARCH}"
    )

    message(STATUS "Using FluidSynth fallback from Windows externals")
  else()
    message(FATAL_ERROR "Failed to locate FluidSynth or library too old")
  endif()
endif()

if(APPLE)
  # Homebrew installs Qt (up to at least 5.9.1) in /usr/local/qt*, ensure it can
  # be found by CMake since it is not in the default /usr/local prefix.
  if(EXISTS /usr/local/opt/qt5)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt5")
  endif()

  if(EXISTS /usr/local/opt/qt6)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt6")
  endif()
endif()

if((NOT Qt5_DIR OR NOT Qt6_DIR) AND MSVC)
  set(Qt5_DIR
      "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/qt5-msvc2019-x64/lib/cmake/Qt5")
  message(STATUS "Using Qt5 fallback")
endif()

find_package(
  QT NAMES Qt6 Qt5
  COMPONENTS Gui Widgets Svg
  REQUIRED)
find_package(
  Qt${QT_VERSION_MAJOR} 5.9
  COMPONENTS Gui Widgets Svg
  REQUIRED)
message(STATUS "Using Qt ${QT_VERSION}")

# Workaround for QTBUG-57886
set_property(TARGET Qt${QT_VERSION_MAJOR}::Core
             PROPERTY INTERFACE_COMPILE_FEATURES "")

set(CMAKE_AUTOMOC ON)
if(${QT_VERSION_MAJOR} EQUAL 6)
  qt6_add_resources(UI_RESOURCES
                    "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")
elseif(${QT_VERSION_MAJOR} EQUAL 5)
  qt5_add_resources(UI_RESOURCES
                    "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")
endif()

if(WIN32)
  set(GUI_TYPE WIN32)
elseif(APPLE)
  set(GUI_TYPE MACOSX_BUNDLE)
endif()

add_executable(
  vgmtrans
  ${GUI_TYPE}
  ${UI_RESOURCES}
  About.cpp
  About.h
  IconBar.cpp
  IconBar.h
  MainWindow.cpp
  MainWindow.h
  MenuBar.cpp
  MenuBar.h
  MusicPlayer.cpp
  MusicPlayer.h
  QtVGMRoot.cpp
  QtVGMRoot.h
  Logger.cpp
  Logger.h
  main_ui.cpp
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
  workarea/VGMFileListView.cpp
  workarea/VGMFileListView.h
  workarea/qhexview/document/buffer/qfilebuffer.cpp
  workarea/qhexview/document/buffer/qhexbuffer.cpp
  workarea/qhexview/document/buffer/qmemorybuffer.cpp
  workarea/qhexview/document/buffer/qmemoryrefbuffer.cpp
  workarea/qhexview/document/commands/hexcommand.cpp
  workarea/qhexview/document/commands/insertcommand.cpp
  workarea/qhexview/document/commands/removecommand.cpp
  workarea/qhexview/document/commands/replacecommand.cpp
  workarea/qhexview/document/qhexcursor.cpp
  workarea/qhexview/document/qhexdocument.cpp
  workarea/qhexview/document/qhexmetadata.cpp
  workarea/qhexview/qhexview.cpp)

target_include_directories(vgmtrans PRIVATE . ./util ./workarea)

target_link_libraries(
  vgmtrans
  PRIVATE g_options
          g_warnings
          vgmtranscore
          Qt${QT_VERSION_MAJOR}::Widgets
          Qt${QT_VERSION_MAJOR}::Svg
          gsl-lite
          FluidSynth::FluidSynth)

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set(BUNDLE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VGMTrans.app")

  set_target_properties(
    vgmtrans
    PROPERTIES MACOSX_BUNDLE true
               MACOSX_BUNDLE_INFO_PLIST
               "${CMAKE_CURRENT_LIST_DIR}/resources/Info.plist.in"
               OUTPUT_NAME VGMTrans)

  target_sources(vgmtrans
                 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns")
  set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns"
                              PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  target_sources(vgmtrans PRIVATE "${CMAKE_SOURCE_DIR}/bin/mame_roms.xml")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/bin/mame_roms.xml"
                              PROPERTIES MACOSX_PACKAGE_LOCATION MacOS)
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  # Make sure the executable ends up in the binary directory without any
  # additional folders
  set_target_properties(
    vgmtrans PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<$<CONFIG:Debug>:>")
  target_sources(vgmtrans
                 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/VGMTrans.rc")

  # Copy ROMs definitions for CPS
  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/bin/mame_roms.xml"
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

  get_target_property(MOC_EXECUTABLE_LOCATION Qt${QT_VERSION_MAJOR}::moc
                      IMPORTED_LOCATION)
  get_filename_component(QT_BIN_LOCATION "${MOC_EXECUTABLE_LOCATION}" DIRECTORY)
  find_program(WINDEPLOYQT windeployqt HINTS "${QT_BIN_LOCATION}")

  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E env PATH="${QT_BIN_LOCATION}" "${WINDEPLOYQT}"
      --plugindir="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/QtPlugins"
      --libdir="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      $<IF:$<CONFIG:Debug>,--debug,--release> --no-translations
      --no-compiler-runtime --no-angle --no-opengl-sw "$<TARGET_FILE:vgmtrans>")

  file(WRITE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/qt.conf"
       "[Paths]\r\nPlugins = ./QtPlugins")

  get_target_property(FLUIDSYNTH_DLL_LOCATION FluidSynth::FluidSynth
                      IMPORTED_LOCATION)
  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUIDSYNTH_DLL_LOCATION}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
  install(TARGETS vgmtrans DESTINATION ${CMAKE_INSTALL_PREFIX})
  install(
    FILES resources/vgmtrans.png
    DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/512x512/apps")
  install(FILES resources/VGMTrans.desktop
          DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
endif()

set(CPACK_PACKAGE_NAME "vgmtrans")
set(CPACK_PACKAGE_VENDOR "VGMTrans Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A converter for sequenced proprietary videogame music formats")
set(CPACK_RPM_PACKAGE_LICENSE ZLIB)

set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR "ZIP")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES "/build.*/;/\\\\.git/")
list(APPEND CPACK_SOURCE_IGNORE_FILES "${CMAKE_BINARY_DIR}")

include(CPack)

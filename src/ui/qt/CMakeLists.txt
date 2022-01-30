add_subdirectory("${PROJECT_SOURCE_DIR}/lib/gsl-lite"
                 "${CMAKE_CURRENT_BINARY_DIR}/gsl-lite" EXCLUDE_FROM_ALL)

add_subdirectory("${PROJECT_SOURCE_DIR}/lib/bass"
                 "${CMAKE_CURRENT_BINARY_DIR}/bass" EXCLUDE_FROM_ALL)

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

if(Qt6_DIR)
  set(Qt_DIR "${Qt6_DIR}")
elseif(Qt5_DIR)
  set(Qt_DIR "${Qt5_DIR}")
endif()

if(NOT Qt_DIR AND MSVC)
  set(Qt_DIR
      "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/qt5-msvc2019-x64/lib/cmake/Qt5")
  set(Qt5_DIR ${Qt_DIR})
  message(STATUS "Using Qt5 fallback")
endif()

find_package(
  Qt NAMES Qt6 Qt5
  COMPONENTS Gui Widgets Svg
  REQUIRED)
find_package(
  Qt${Qt_VERSION_MAJOR} 5.9
  COMPONENTS Gui Widgets Svg
  REQUIRED)
message(STATUS "Using Qt ${Qt_VERSION}")

# Workaround for QTBUG-57886
set_property(TARGET Qt${Qt_VERSION_MAJOR}::Core
             PROPERTY INTERFACE_COMPILE_FEATURES "")

set(CMAKE_AUTOMOC ON)
if(${Qt_VERSION_MAJOR} EQUAL 6)
  qt6_add_resources(UI_RESOURCES
                    "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")
elseif(${Qt_VERSION_MAJOR} EQUAL 5)
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
  Logger.cpp
  Logger.h
  MainWindow.cpp
  MainWindow.h
  ManualCollectionDialog.cpp
  ManualCollectionDialog.h
  MenuBar.cpp
  MenuBar.h
  QtVGMRoot.cpp
  QtVGMRoot.h
  SequencePlayer.cpp
  SequencePlayer.h
  main_ui.cpp
  util/Helpers.cpp
  util/Helpers.h
  util/MarqueeLabel.cpp
  util/MarqueeLabel.h
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
          Qt${Qt_VERSION_MAJOR}::Widgets
          Qt${Qt_VERSION_MAJOR}::Svg
          gsl-lite
          BASS::MIDI)

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

  get_target_property(BASS_DLL_LOCATION BASS::BASS
                      IMPORTED_LOCATION)
  add_custom_command(TARGET vgmtrans
                      POST_BUILD
                      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASS_DLL_LOCATION}"
                      "${BUNDLE_PATH}/Contents/MacOS/")
  get_target_property(BASSMIDI_DLL_LOCATION BASS::MIDI
                      IMPORTED_LOCATION)
  add_custom_command(TARGET vgmtrans
                      POST_BUILD
                      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASSMIDI_DLL_LOCATION}"
                      "${BUNDLE_PATH}/Contents/MacOS/")
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

  get_target_property(MOC_EXECUTABLE_LOCATION Qt${Qt_VERSION_MAJOR}::moc
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

  get_target_property(BASS_DLL_LOCATION BASS::BASS
                      IMPORTED_LOCATION)
  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASS_DLL_LOCATION}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

  get_target_property(BASSMIDI_DLL_LOCATION BASS::MIDI
                      IMPORTED_LOCATION)
  add_custom_command(
  TARGET vgmtrans
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASSMIDI_DLL_LOCATION}"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
  install(TARGETS vgmtrans DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
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

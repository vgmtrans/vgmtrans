# ~~~
# Dependencies
# ~~~

add_subdirectory("${PROJECT_SOURCE_DIR}/lib/bass"
                 "${CMAKE_CURRENT_BINARY_DIR}/bass" EXCLUDE_FROM_ALL)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # Homebrew installs Qt (up to at least 5.9.1) in /usr/local/qt*, ensure it can
  # be found by CMake since it is not in the default /usr/local prefix.
  if(EXISTS /usr/local/opt/qt6)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt6")
  endif()
endif()

if(NOT Qt6_DIR AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(STATUS "Using vendored Qt6")
  set(Qt6_DIR
      "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/qt6.6.1-x86_64-pc-windows-msvc/lib/cmake/Qt6"
  )
  if(NOT EXISTS "${Qt6_DIR}")
    message(SEND_ERROR "Could not find `${Qt6_DIR}`")
    message(
      SEND_ERROR
        "Did you forget to run `git submodule update --init --recursive`?")
  endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
message(STATUS "Using Qt ${Qt6_VERSION}")

# ~~~
# Build
# ~~~

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(GUI_TYPE MACOSX_BUNDLE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(GUI_TYPE WIN32)
endif()

add_executable(
  vgmtrans
  ${GUI_TYPE}
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
  services/MenuManager.cpp
  services/MenuManager.h
  services/Settings.cpp
  services/Settings.h
  services/NotificationCenter.cpp
  services/NotificationCenter.h
  services/commands/Command.h
  services/commands/SaveCommands.h
  services/commands/GeneralCommands.h
  util/Colors.h
  util/Helpers.cpp
  util/Helpers.h
  util/LambdaEventFilter.h
  util/UIHelpers.cpp
  util/UIHelpers.h
  widgets/MarqueeLabel.cpp
  widgets/MarqueeLabel.h
  widgets/SnappingSplitter.cpp
  widgets/SnappingSplitter.h
  widgets/StatusBarContent.cpp
  widgets/StatusBarContent.h
  widgets/TableView.cpp
  widgets/TableView.h
  widgets/TitleBar.cpp
  widgets/TitleBar.h
  workarea/HexView.cpp
  workarea/HexView.h
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
  workarea/VGMFileListView.h)

target_include_directories(vgmtrans PRIVATE . ./util ./widgets ./workarea)

target_link_libraries(
  vgmtrans
  PRIVATE vgmtranscore
          Qt::Widgets
          Qt::Svg
          BASS::MIDI)

# Workaround macdeployqt not correctly deploying QtDbus
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  find_package(Qt6 REQUIRED COMPONENTS DBus)
  target_link_libraries(vgmtrans PUBLIC Qt::DBus)
endif()

set_target_properties(vgmtrans PROPERTIES AUTOMOC ON)
qt_add_resources(UI_RESOURCES
                 "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")
target_sources(vgmtrans PRIVATE ${UI_RESOURCES})

# ~~~
# Install & package
# ~~~
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

  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/bin/mame_roms.xml"
    "${BUNDLE_PATH}/Contents/Resources/")

  get_target_property(BASS_DLL_LOCATION BASS::BASS IMPORTED_LOCATION)
  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASS_DLL_LOCATION}"
            "${BUNDLE_PATH}/Contents/MacOS/")
  get_target_property(BASSMIDI_DLL_LOCATION BASS::MIDI IMPORTED_LOCATION)
  add_custom_command(
    TARGET vgmtrans
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
      --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw
      "$<TARGET_FILE:vgmtrans>")

  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_LIST_DIR}/qt.conf"
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

  get_target_property(BASS_DLL_LOCATION BASS::BASS IMPORTED_LOCATION)
  add_custom_command(
    TARGET vgmtrans
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASS_DLL_LOCATION}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

  get_target_property(BASSMIDI_DLL_LOCATION BASS::MIDI IMPORTED_LOCATION)
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

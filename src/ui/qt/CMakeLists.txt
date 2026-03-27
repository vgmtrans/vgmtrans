find_package(Qt6 REQUIRED COMPONENTS Core Gui ShaderTools Widgets Svg)
# GuiPrivate was moved to a CMake component starting Qt 6.9,
# so look for it separately to avoid breaking older versions
find_package(Qt6 COMPONENTS GuiPrivate QUIET)
message(STATUS "Using Qt: ${Qt6_VERSION}")
message(STATUS "Has own QtGuiPrivate: ${Qt6GuiPrivate_FOUND}")

# D-Bus is used on Linux for the XDG FileTransfer portal
# and on macOS to workaround a macdeployqt bug
if(CMAKE_SYSTEM_NAME MATCHES "Darwin|FreeBSD|OpenBSD|Linux")
  find_package(Qt6 COMPONENTS DBus QUIET)
endif()

# ~~~
# Build
# ~~~

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(GUI_TYPE MACOSX_BUNDLE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(GUI_TYPE WIN32)
endif()

add_executable(vgmtrans ${GUI_TYPE})
target_sources(vgmtrans
  PRIVATE
    About.cpp
    IconBar.cpp
    Logger.cpp
    MainWindow.cpp
    ManualCollectionDialog.cpp
    MenuBar.cpp
    QtVGMRoot.cpp
    ReportDialog.cpp
    SequencePlayer.cpp
    main_ui.cpp
    services/MenuManager.cpp
    services/Settings.cpp
    services/NotificationCenter.cpp
    util/Helpers.cpp
    util/UIHelpers.cpp
    widgets/ItemViewDensity.cpp
    widgets/SeekBar.cpp
    widgets/MarqueeLabel.cpp
    widgets/SnappingSplitter.cpp
    widgets/StatusBarContent.cpp
    widgets/Toast.cpp
    widgets/ToastHost.cpp
    widgets/TableView.cpp
    widgets/TitleBar.cpp
    workarea/hexview/HexView.cpp
    workarea/hexview/HexViewRhiHost.cpp
    workarea/hexview/HexViewRhiRenderer.cpp
    workarea/hexview/HexViewRhiWidget.cpp
    workarea/hexview/HexViewRhiWindow.cpp
    workarea/MdiArea.cpp
    workarea/RawFileListView.cpp
    workarea/VGMCollListView.cpp
    workarea/VGMCollView.cpp
    workarea/VGMFileTreeView.cpp
    workarea/VGMFileView.cpp
    workarea/VGMFileListView.cpp

  PRIVATE
    FILE_SET HEADERS TYPE HEADERS
    FILES
      About.h
      IconBar.h
      Logger.h
      MainWindow.h
      ManualCollectionDialog.h
      MenuBar.h
      QtVGMRoot.h
      ReportDialog.h
      SequencePlayer.h
      services/MenuManager.h
      services/NotificationCenter.h
      services/Settings.h
      services/commands/Command.h
      services/commands/GeneralCommands.h
      services/commands/SaveCommands.h
      services/commands/VGMCollCommands.h
  PRIVATE
    FILE_SET headers_util TYPE HEADERS BASE_DIRS util
    FILES
      util/Colors.h
      util/Helpers.h
      util/LambdaEventFilter.h
      util/Metrics.h
      util/TintableSvgIconEngine.h
      util/UIHelpers.h
  PRIVATE
    FILE_SET headers_widgets TYPE HEADERS BASE_DIRS widgets
    FILES
      widgets/FixedHeightListDelegate.h
      widgets/ItemViewDensity.h
      widgets/SeekBar.h
      widgets/MarqueeLabel.h
      widgets/SnappingSplitter.h
      widgets/StatusBarContent.h
      widgets/TableView.h
      widgets/TitleBar.h
      widgets/Toast.h
      widgets/ToastHost.h
  PRIVATE
    FILE_SET headers_workarea TYPE HEADERS BASE_DIRS workarea
    FILES
      workarea/MdiArea.h
      workarea/RawFileListView.h
      workarea/VGMCollListView.h
      workarea/VGMCollView.h
      workarea/VGMFileListView.h
      workarea/VGMFileTreeView.h
      workarea/VGMFileView.h
      workarea/hexview/HexView.h
      workarea/hexview/HexViewFrameData.h
      workarea/hexview/HexViewInput.h
      workarea/hexview/HexViewRhiEventForwarder.h
      workarea/hexview/HexViewRhiHost.h
      workarea/hexview/HexViewRhiInputCoalescer.h
      workarea/hexview/HexViewRhiRenderer.h
      workarea/hexview/HexViewRhiWidget.h
      workarea/hexview/HexViewRhiWindow.h
)

if(APPLE)
  target_sources(vgmtrans PRIVATE util/Helpers_mac.mm)
  set_target_properties(vgmtrans PROPERTIES OBJCXX_STANDARD 20)
endif()

target_compile_features(vgmtrans PRIVATE cxx_std_20)

target_link_libraries(
  vgmtrans
  PRIVATE vgmtranscore
          Qt6::Widgets
          Qt6::GuiPrivate
          Qt6::Svg
          BASS::MIDI)

if(Qt6DBus_FOUND)
  target_link_libraries(vgmtrans PRIVATE Qt6::DBus)
  target_compile_definitions(vgmtrans PRIVATE VGMTRANS_HAVE_DBUS)
else()
  if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    message(WARNING "Qt6::DBus not found, Wayland drag-and-drop will not work")
  elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    message(WARNING "Qt6::DBus not found, final .app bundle may not work")
  endif()
endif()

qt_add_shaders(
  vgmtrans
  "hexview_shaders"
  PREFIX "/shaders"
  BASE "workarea/hexview/shaders"
  FILES
    workarea/hexview/shaders/hexquad.vert
    workarea/hexview/shaders/hexquad.frag
    workarea/hexview/shaders/hexglyph.vert
    workarea/hexview/shaders/hexglyph.frag
    workarea/hexview/shaders/hexscreen.vert
    workarea/hexview/shaders/hexedge.vert
    workarea/hexview/shaders/hexedge.frag
    workarea/hexview/shaders/hexcomposite.frag
)

# Workaround macdeployqt not correctly deploying QtDbus
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_libraries(vgmtrans PUBLIC Qt6::DBus)
endif()

set_target_properties(vgmtrans PROPERTIES AUTOMOC ON)
qt_add_resources(UI_RESOURCES
                 "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")
target_sources(vgmtrans PRIVATE ${UI_RESOURCES})

# DLLs need to live next to the main executable for development testing
if(WIN32)
  add_custom_command(TARGET vgmtrans POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:vgmtrans>
        $<TARGET_FILE_DIR:vgmtrans>
    COMMAND_EXPAND_LISTS
  )
endif()

# ~~~
# Install & package
# ~~~

set(BUNDLE_CHANGELOG_FILE "")
set(METAINFO_RELEASE_DESCRIPTION_XML "")
set(METAINFO_RELEASE_TYPE "")
set(METAINFO_RELEASE_URL_XML "")

if(INSTALL_CHANGELOG)
  find_package(Python3 3.10)
  if(NOT Python3_FOUND)
    message(FATAL_ERROR "Option INSTALL_CHANGELOG=ON requires Python 3.10+")
  endif()

  message(STATUS "Generating Markdown release notes...")
  set(GENERATED_CHANGELOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  file(MAKE_DIRECTORY "${GENERATED_CHANGELOG_DIR}")
  set(BUNDLE_CHANGELOG_FILE "${GENERATED_CHANGELOG_DIR}/CHANGELOG.md")
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}" "${BUILD_CHANGELOG_SCRIPT}"
      --input "${PROJECT_CHANGELOG_FILE}"
      --current-version "${CHANGELOG_CURRENT_VERSION}"
      --format markdown
      --output "${BUNDLE_CHANGELOG_FILE}"
    COMMAND_ERROR_IS_FATAL ANY)

  if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
    message(STATUS "Generating AppStream release notes...")
    set(METAINFO_RELEASE_DESCRIPTION_FILE
        "${GENERATED_CHANGELOG_DIR}/metainfo-release-description.xml")
    execute_process(
      COMMAND
        "${Python3_EXECUTABLE}" "${BUILD_CHANGELOG_SCRIPT}"
        --input "${PROJECT_CHANGELOG_FILE}"
        --current-version "${CHANGELOG_CURRENT_VERSION}"
        --format appstream
        --output "${METAINFO_RELEASE_DESCRIPTION_FILE}"
      COMMAND_ERROR_IS_FATAL ANY)
    file(READ "${METAINFO_RELEASE_DESCRIPTION_FILE}" METAINFO_RELEASE_DESCRIPTION_XML)

    if(CHANGELOG_CURRENT_VERSION STREQUAL "Unreleased")
      set(METAINFO_RELEASE_TYPE " type=\"development\"")
    else()
      set(METAINFO_RELEASE_URL_XML
"      <url type=\"details\">https://github.com/vgmtrans/vgmtrans/releases/tag/${VCS_EXACT_TAG}</url>")
    endif()
  endif()

  install(FILES "${BUNDLE_CHANGELOG_FILE}" DESTINATION ".")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set_target_properties(
    vgmtrans
    PROPERTIES MACOSX_BUNDLE true
               MACOSX_BUNDLE_INFO_PLIST
               "${CMAKE_CURRENT_LIST_DIR}/resources/Info.plist.in"
               OUTPUT_NAME VGMTrans)

  target_sources(vgmtrans
                 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns"
                         "${PROJECT_SOURCE_DIR}/bin/mame_roms.json")
                         
  set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns"
    "${PROJECT_SOURCE_DIR}/bin/mame_roms.json"
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
  )

  set_target_properties(vgmtrans PROPERTIES
      INSTALL_RPATH "@executable_path/../Frameworks"
      INSTALL_RPATH_USE_LINK_PATH TRUE
  )

  install(FILES
          $<TARGET_FILE:BASS::BASS>
          $<TARGET_FILE:BASS::MIDI>
          DESTINATION "VGMTrans.app/Contents/Frameworks"
  )
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  target_sources(vgmtrans
                 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/VGMTrans.rc")
                 
  install(FILES "${PROJECT_SOURCE_DIR}/bin/mame_roms.json"
          DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(FILES $<TARGET_RUNTIME_DLLS:vgmtrans>
          DESTINATION "${CMAKE_INSTALL_BINDIR}")

	set(DEPLOY_EXTRA_OPTIONS
      --no-system-d3d-compiler
      --no-opengl-sw
      --skip-plugin-types tls
  )
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
  set(APP_ID "io.github.vgmtrans.vgmtrans")

  install(FILES
          $<TARGET_FILE:BASS::BASS>
          $<TARGET_FILE:BASS::MIDI>
	        DESTINATION "${CMAKE_INSTALL_LIBDIR}")

  set_target_properties(vgmtrans PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  )

  install(FILES "${CMAKE_SOURCE_DIR}/bin/mame_roms.json"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/vgmtrans")

  install(FILES resources/vgmtrans.png
          DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
          RENAME "${APP_ID}.png")
  install(FILES resources/VGMTrans.desktop
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
          RENAME "${APP_ID}.desktop")

  configure_file(resources/metainfo.xml.in "${CMAKE_CURRENT_BINARY_DIR}/metainfo.xml" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/metainfo.xml"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
          RENAME "${APP_ID}.metainfo.xml")

  install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses/"
          DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/vgmtrans")
  install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
          DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/vgmtrans")
endif()

install(TARGETS vgmtrans
        BUNDLE DESTINATION .
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if (NOT NO_QT_DEPLOY)
  qt_generate_deploy_app_script(
      TARGET vgmtrans
      OUTPUT_SCRIPT deploy_script
      NO_UNSUPPORTED_PLATFORM_ERROR
      NO_TRANSLATIONS
      NO_COMPILER_RUNTIME
      DEPLOY_TOOL_OPTIONS ${DEPLOY_EXTRA_OPTIONS}
  )
  install(SCRIPT ${deploy_script})
endif()

# ~~~
# CPack Setup
# ~~~
set(CPACK_PACKAGE_NAME "vgmtrans")
set(CPACK_PACKAGE_VENDOR "VGMTrans Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A converter for sequenced proprietary videogame music formats")

# This is unbranded because generating a .DS_Store on the fly is a nightmare
# and storing it in the repo has all kind of issues. CI can use a fancy DMG
# creation method, for local testing this is fine
if("DragNDrop" IN_LIST CPACK_GENERATOR)
  set(CPACK_DMG_VOLUME_NAME "VGMTrans")  

  install(FILES "${PROJECT_SOURCE_DIR}/bin/dmg/README.txt" DESTINATION "." COMPONENT Runtime)
  install(PROGRAMS "${PROJECT_SOURCE_DIR}/bin/dmg/selfsign" DESTINATION "." RENAME "Unblock & Sign" COMPONENT Runtime)
  if(INSTALL_CHANGELOG)
    install(FILES "${BUNDLE_CHANGELOG_FILE}" DESTINATION "." COMPONENT Runtime)
  endif()
endif()

if(NOT CPACK_GENERATOR)
  message(STATUS "Falling back to default CPack generator: ZIP")
	set(CPACK_GENERATOR "ZIP")
endif()

if(NOT CPACK_SOURCE_GENERATOR)
  message(STATUS "Falling back to default source archive CPack generator: ZIP")
	set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
endif()

# Prevent hell from populating source packages
set(CPACK_SOURCE_IGNORE_FILES "/build.*/;/\\\\.git/")
list(APPEND CPACK_SOURCE_IGNORE_FILES "${CMAKE_BINARY_DIR}")

include(CPack)

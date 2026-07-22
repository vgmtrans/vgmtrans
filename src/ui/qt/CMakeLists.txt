set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
message(STATUS "Using Qt: ${Qt6_VERSION}")

if(NOT TARGET QWindowKit::Widgets)
  set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "" FORCE)
  set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "" FORCE)
  set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/qwindowkit
                   ${CMAKE_CURRENT_BINARY_DIR}/lib/qwindowkit
                   EXCLUDE_FROM_ALL SYSTEM)
endif()

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

# Qt-facing state and models live in a small library so their value-only
# dependency boundary can be tested without constructing the full window.
add_library(vgmtransqtvalue STATIC)
vgmtrans_enable_project_warnings(vgmtransqtvalue)
target_sources(vgmtransqtvalue
  PRIVATE
    application/WorkspaceController.cpp
    models/ValueModels.cpp
  PUBLIC
    FILE_SET HEADERS TYPE HEADERS
    FILES
      application/WorkspaceController.h
      models/ValueModels.h
)
set_target_properties(vgmtransqtvalue PROPERTIES AUTOMOC ON)
target_compile_features(vgmtransqtvalue PUBLIC cxx_std_20)
target_include_directories(vgmtransqtvalue PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(vgmtransqtvalue PUBLIC vgmtransvalueformats Qt6::Core Qt6::Gui Qt6::Svg)

add_executable(vgmtrans ${GUI_TYPE})
vgmtrans_enable_project_warnings(vgmtrans)
target_sources(vgmtrans
  PRIVATE
    About.cpp
    Logger.cpp
    PlaybackControls.cpp
    ReportDialog.cpp
    SequencePlayer.cpp
    MainWindow.cpp
    MainWindowDockLayout.cpp
    MenuBar.cpp
    main_ui.cpp
    services/Settings.cpp
    util/ColorHelpers.cpp
    util/ExternalUrl.cpp
    util/InstructionHintLayout.cpp
    util/UIHelpers.cpp
    widgets/EmptyStateWidget.cpp
    widgets/ItemViewDensity.cpp
    widgets/SnappingSplitter.cpp
    widgets/SeekBar.cpp
    widgets/StatusBarContent.cpp
    widgets/TableView.cpp
    widgets/TitleBar.cpp
    widgets/Toast.cpp
    widgets/ToastHost.cpp
    widgets/ToastWindow.cpp
    widgets/Windows11ProxyStyle.cpp
    widgets/WindowBar.cpp
    workarea/CollectionListView.cpp
    workarea/MdiArea.cpp
    workarea/SourceInspectorPresentation.cpp
    workarea/VGMFileTreeView.cpp
    workarea/VGMFileView.cpp
    workarea/hexview/HexView.cpp
    workarea/hexview/HexViewRhiHost.cpp
    workarea/hexview/HexViewRhiRenderer.cpp
    workarea/hexview/HexViewRhiWidget.cpp
    workarea/hexview/HexViewRhiWindow.cpp
  PRIVATE
    FILE_SET HEADERS TYPE HEADERS
    FILES
      About.h
      Logger.h
      PlaybackPosition.h
      PlaybackControls.h
      ReportDialog.h
      SequencePlayer.h
      MainWindow.h
      MainWindowDockLayout.h
      MenuBar.h
      services/Settings.h
      util/ColorHelpers.h
      util/ExternalUrl.h
      util/Colors.h
      util/InstructionHintLayout.h
      util/LambdaEventFilter.h
      util/Metrics.h
      util/NonTransientScrollBarStyle.h
      util/TintableSvgIconEngine.h
      util/UIHelpers.h
      widgets/EmptyStateWidget.h
      widgets/FixedHeightListDelegate.h
      widgets/ItemViewDensity.h
      widgets/SnappingSplitter.h
      widgets/SeekBar.h
      widgets/StatusBarContent.h
      widgets/TableView.h
      widgets/TitleBar.h
      widgets/Toast.h
      widgets/ToastHost.h
      widgets/ToastWindow.h
      widgets/Windows11ProxyStyle.h
      widgets/WindowBar.h
      workarea/CollectionListView.h
      workarea/MdiArea.h
      workarea/SourceInspectorPresentation.h
      workarea/SplitterSnapProvider.h
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
      workarea/rhi/RhiWindowDragDropEvents.h
      workarea/rhi/RhiWindowMainWindowLocator.h
)
target_compile_features(vgmtrans PRIVATE cxx_std_20)
target_include_directories(vgmtrans PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/util
  ${CMAKE_CURRENT_LIST_DIR}/widgets
  ${CMAKE_CURRENT_LIST_DIR}/workarea)
target_link_libraries(vgmtrans PRIVATE vgmtransqtvalue vgmtransversion BASS::MIDI
                      QWindowKit::Widgets Qt6::Widgets Qt6::GuiPrivate Qt6::Svg)
set_target_properties(vgmtrans PROPERTIES AUTOMOC ON)

if(APPLE)
  target_sources(vgmtrans PRIVATE
    util/ExternalUrl_mac.mm
    widgets/ToastWindow_mac.mm)
  set_target_properties(vgmtrans PROPERTIES OBJCXX_STANDARD 20)
endif()

# These files contain UI behavior that still needs to be reworked against value
# IDs and snapshots. Keep them visible in the project as reference
# implementations; they are intentionally not linked into the value-only UI.
set(VGMTRANS_UI_VALUE_PORT_REFERENCE_SOURCES
  ManualCollectionDialog.cpp
  ManualCollectionDialog.h
  services/MenuManager.cpp
  services/MenuManager.h
  services/NotificationCenter.cpp
  services/NotificationCenter.h
  services/commands/Command.h
  services/commands/GeneralCommands.h
  services/commands/SaveCommands.h
  services/commands/StitchCommands.cpp
  services/commands/StitchCommands.h
  services/commands/VGMCollCommands.h
  widgets/StitchUI.cpp
  widgets/StitchUI.h
  workarea/RawFileListView.cpp
  workarea/RawFileListView.h
  workarea/VGMCollListView.cpp
  workarea/VGMCollListView.h
  workarea/VGMCollView.cpp
  workarea/VGMCollView.h
  workarea/VGMFileListView.cpp
  workarea/VGMFileListView.h
)
add_custom_target(vgmtrans-ui-value-port-reference
  SOURCES ${VGMTRANS_UI_VALUE_PORT_REFERENCE_SOURCES})
set_target_properties(vgmtrans-ui-value-port-reference PROPERTIES
  FOLDER "VGMTrans/UI migration")

set(VGMTRANS_HEX_VIEW_SHADER_SOURCES
  workarea/hexview/shaders/hexquad.vert
  workarea/hexview/shaders/hexquad.frag
  workarea/hexview/shaders/hexglyph.vert
  workarea/hexview/shaders/hexglyph.frag
  workarea/hexview/shaders/hexscreen.vert
  workarea/hexview/shaders/hexedge.vert
  workarea/hexview/shaders/hexedge_effect.frag
  workarea/hexview/shaders/hexcomposite.frag)

# Keep the HexView shader bundle attached through one helper so other UI test or
# preview targets can opt into the same renderer resources later.
function(vgmtrans_add_hex_view_shaders target)
  find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
  target_compile_definitions(${target} PRIVATE VGMTRANS_VALUE_HEX_VIEW)
  qt_add_shaders(${target}
    "hexview_shaders"
    PREFIX "/shaders"
    BASE "workarea/hexview/shaders"
    FILES ${VGMTRANS_HEX_VIEW_SHADER_SOURCES})
endfunction()

vgmtrans_add_hex_view_shaders(vgmtrans)

if(Qt6DBus_FOUND)
  target_link_libraries(vgmtrans PRIVATE Qt6::DBus)
  target_compile_definitions(vgmtrans PRIVATE VGMTRANS_HAVE_DBUS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(WARNING "Qt6::DBus not found, Wayland drag-and-drop will not work")
endif()

qt_add_resources(UI_RESOURCES
                 "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")
target_sources(vgmtrans PRIVATE ${UI_RESOURCES})

# DLLs need to live next to the main executable for development testing.
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

  target_sources(vgmtrans PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns")
  set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(vgmtrans PROPERTIES
      INSTALL_RPATH "@executable_path/../Frameworks"
      INSTALL_RPATH_USE_LINK_PATH TRUE)
  install(FILES $<TARGET_FILE:BASS::BASS> $<TARGET_FILE:BASS::MIDI>
          DESTINATION "VGMTrans.app/Contents/Frameworks")
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  target_sources(vgmtrans PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/VGMTrans.rc")
  install(FILES $<TARGET_RUNTIME_DLLS:vgmtrans>
          DESTINATION "${CMAKE_INSTALL_BINDIR}")

  set(DEPLOY_EXTRA_OPTIONS
      --no-system-d3d-compiler
      --no-opengl-sw
      --skip-plugin-types tls)
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
  set(APP_ID "io.github.vgmtrans.vgmtrans")

  install(FILES $<TARGET_FILE:BASS::BASS> $<TARGET_FILE:BASS::MIDI>
          DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  set_target_properties(vgmtrans PROPERTIES
      INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

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
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(NOT NO_QT_DEPLOY)
  qt_generate_deploy_app_script(
      TARGET vgmtrans
      OUTPUT_SCRIPT deploy_script
      NO_UNSUPPORTED_PLATFORM_ERROR
      NO_TRANSLATIONS
      NO_COMPILER_RUNTIME
      DEPLOY_TOOL_OPTIONS ${DEPLOY_EXTRA_OPTIONS})
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
# creation method, for local testing this is fine.
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

# Prevent generated build trees from entering source packages.
set(CPACK_SOURCE_IGNORE_FILES "/build.*/;/\\.git/")
list(APPEND CPACK_SOURCE_IGNORE_FILES "${CMAKE_BINARY_DIR}")

include(CPack)

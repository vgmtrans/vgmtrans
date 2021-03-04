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

find_package(Qt5 REQUIRED COMPONENTS Gui Widgets Svg)
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

add_executable(vgmtrans
        ${GUI_TYPE}
        ${UI_RESOURCES}
        mainwindow.cpp
        mainwindow.h
        QtVGMRoot.cpp
        QtVGMRoot.h
        main_ui.cpp
        util/Helpers.cpp
        util/Helpers.h
        workarea/MdiArea.cpp
        workarea/MdiArea.h
        workarea/RawFileListView.cpp
        workarea/RawFileListView.h
        workarea/VGMCollListView.cpp
        workarea/VGMCollListView.h
        workarea/VGMFileTreeView.cpp
        workarea/VGMFileTreeView.h
        workarea/VGMFileView.cpp
        workarea/VGMFileView.h
        workarea/VGMFileListView.cpp
        workarea/VGMFileListView.h
        workarea/VGMFileItemModel.cpp
        workarea/VGMFileItemModel.h
        workarea/HexView.cpp
        workarea/HexView.h)

target_include_directories(vgmtrans PRIVATE . ./util ./workarea)

target_link_libraries(vgmtrans
        PRIVATE g_options
        g_warnings
        vgmtranscore
        vgmtranscommon
        Qt5::Widgets
        Qt5::Svg)

#TODO: Add icon for win32 and macOS, add xdg .desktop file
if(APPLE)
    set(BUNDLE_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VGMTrans.app)

    set_target_properties(vgmtrans
            PROPERTIES MACOSX_BUNDLE
            true
            MACOSX_BUNDLE_INFO_PLIST
            ${CMAKE_CURRENT_LIST_DIR}/resources/Info.plist.in
            OUTPUT_NAME
            VGMTrans)
else()
    install(TARGETS vgmtrans DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()
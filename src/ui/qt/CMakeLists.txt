find_package(Qt5 5.9 REQUIRED COMPONENTS Gui Widgets)
message(STATUS "Using Qt version ${Qt5Core_VERSION}")

set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES "")
set(CMAKE_AUTOMOC ON)
qt5_add_resources(UI_RESOURCES "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")

add_executable(vgmtrans
               HeaderContainer.cpp
               HeaderContainer.h
               Helpers.cpp
               Helpers.h
               HexView.cpp
               HexView.h
               IconBar.cpp
               IconBar.h
               Logger.cpp
               Logger.h
               main_ui.cpp
               MainWindow.cpp
               MainWindow.h
               MdiArea.cpp
               MdiArea.h
               MenuBar.cpp
               MenuBar.h
               MusicPlayer.cpp
               MusicPlayer.h
               QtVGMRoot.cpp
               QtVGMRoot.h
               RawFileListView.cpp
               RawFileListView.h
               VGMCollListView.cpp
               VGMCollListView.h
               VGMFileItemModel.cpp
               VGMFileItemModel.h
               VGMFilesList.cpp
               VGMFilesList.h
               VGMFileTreeView.cpp
               VGMFileTreeView.h
               VGMFileView.cpp
               VGMFileView.h
               ${UI_RESOURCES})

target_include_directories(vgmtrans PRIVATE .)

target_link_libraries(vgmtrans
                      PRIVATE g_options
                              g_warnings
                              vgmcore
                              Qt5::Widgets
                              fluidsynth)

target_compile_definitions(vgmtrans PRIVATE QT_USE_QSTRINGBUILDER)

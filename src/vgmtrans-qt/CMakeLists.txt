#TODO: Make optional: no fluidsynth, no playback support.
find_package(FLUIDSYNTH)
if(FLUIDSYNTH_FOUND)
    message(STATUS "Using system FluidSynth ${FLUIDSYNTH_VERSION_STRING}")
else()
    message(FATAL_ERROR "Please install FluidSynth! Bundled version not available")
endif()

find_package(Qt5 5.9 REQUIRED COMPONENTS Gui Widgets)
message(STATUS "Using Qt version ${Qt5Core_VERSION}")

set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES "")
set(CMAKE_AUTOMOC ON)
qt5_add_resources(UI_RESOURCES
                  "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")

add_executable(vgmtrans
               util/HeaderContainer.cpp
               util/HeaderContainer.h
               util/Helpers.cpp
               util/Helpers.h
               workarea/HexView.cpp
               workarea/HexView.h
               workarea/MdiArea.cpp
               workarea/MdiArea.h
               workarea/RawFileListView.cpp
               workarea/RawFileListView.h
               workarea/VGMCollListView.cpp
               workarea/VGMCollListView.h
               workarea/VGMFileItemModel.cpp
               workarea/VGMFileItemModel.h
               workarea/VGMFilesList.cpp
               workarea/VGMFilesList.h
               workarea/VGMFileTreeView.cpp
               workarea/VGMFileTreeView.h
               workarea/VGMFileView.cpp
               workarea/VGMFileView.h
               IconBar.cpp
               IconBar.h
               Logger.cpp
               Logger.h
               main_ui.cpp
               MainWindow.cpp
               MainWindow.h
               MenuBar.cpp
               MenuBar.h
               MusicPlayer.cpp
               MusicPlayer.h
               QtVGMRoot.cpp
               QtVGMRoot.h
               ${UI_RESOURCES})

target_include_directories(vgmtrans
                           PRIVATE ${CMAKE_CURRRENT_LIST_DIR}
                                   ${CMAKE_CURRRENT_LIST_DIR}/util
                                   ${CMAKE_CURRRENT_LIST_DIR}/workarea)

target_link_libraries(vgmtrans
                      PRIVATE g_options
                              g_warnings
                              vgmcore
                              Qt5::Widgets
                              fluidsynth)

target_compile_definitions(vgmtrans PRIVATE QT_USE_QSTRINGBUILDER)

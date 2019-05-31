#TODO: Make optional: no fluidsynth, no playback support.
find_package(FLUIDSYNTH)
if(FLUIDSYNTH_FOUND)
    message(STATUS "Using FluidSynth ${FLUIDSYNTH_VERSION_STRING}")
else()
	if(MSVC)
		add_library(fluidsynth STATIC IMPORTED)
		set_target_properties(fluidsynth PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/externals/win-deps/fluidsynth2-msvc2017-x64/fluidsynth.lib")
		target_include_directories(fluidsynth INTERFACE ${CMAKE_SOURCE_DIR}/externals/win-deps/fluidsynth2-msvc2017-x64/include)
		message(STATUS "Using FluidSynth fallback")
	else()
		message(FATAL_ERROR "Please install FluidSynth! Bundled version not available")
	endif()
endif()


if (NOT Qt5_DIR AND MSVC)
	set(Qt5_DIR "${CMAKE_SOURCE_DIR}/externals/win-deps/qt5-msvc2017-x64/lib/cmake/Qt5")
	message(STATUS "Using Qt5 fallback")
endif()

find_package(Qt5 REQUIRED COMPONENTS Gui Widgets)
message(STATUS "Using Qt version ${Qt5Core_VERSION}")

set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES "")
set(CMAKE_AUTOMOC ON)
qt5_add_resources(UI_RESOURCES
                  "${CMAKE_CURRENT_LIST_DIR}/resources/resources.qrc")

if(WIN32)
	set(GUI_TYPE WIN32)
endif()

add_executable(vgmtrans ${GUI_TYPE}
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
               About.cpp
               About.h
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
                           PRIVATE .
                                   ./util
                                   ./workarea)

target_link_libraries(vgmtrans
                      PRIVATE g_options
                              g_warnings
                              vgmcore
                              Qt5::Widgets
                              fluidsynth)

target_compile_definitions(vgmtrans PRIVATE QT_USE_QSTRINGBUILDER)

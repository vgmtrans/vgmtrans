add_subdirectory("${PROJECT_SOURCE_DIR}/lib/gsl-lite" "${CMAKE_CURRENT_BINARY_DIR}/gsl-lite" EXCLUDE_FROM_ALL)

find_package(FluidSynth)
if(FluidSynth_FOUND AND "${FluidSynth_VERSION}" MATCHES "2(.)*")
    message(STATUS "Using FluidSynth ${FluidSynth_VERSION}")
else()
    if(MSVC)

	message(STATUS Compiling for ${CMAKE_SYSTEM_PROCESSOR})
        if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
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
                "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/solidsynth-msvc2019-${FLUID_LIBRARY_ARCH}/libfluidsynth-2.dll"
        )

        target_include_directories(
                FluidSynth::FluidSynth
                INTERFACE
                "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/solidsynth-msvc2019-${FLUID_LIBRARY_ARCH}")

        message(STATUS "Using FluidSynth fallback from Windows externals")
    else()
        message(FATAL_ERROR "Failed to locate FluidSynth or library too old")
    endif()
endif()

if(APPLE AND EXISTS /usr/local/opt/qt5)
    # Homebrew installs Qt5 (up to at least 5.9.1) in /usr/local/qt5, ensure it
    # can be found by CMake since it is not in the default /usr/local prefix.
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt5")
endif()

if(NOT Qt5_DIR AND MSVC)
    set(Qt5_DIR
            "${CMAKE_SOURCE_DIR}/lib/vgmtrans-ext-win/qt5-msvc2019-x64/lib/cmake/Qt5")
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

add_subdirectory("${PROJECT_SOURCE_DIR}/lib/qhexview" "${CMAKE_CURRENT_BINARY_DIR}/qhexview" EXCLUDE_FROM_ALL)

add_executable(vgmtrans
        ${GUI_TYPE}
        ${UI_RESOURCES}
        About.cpp
        About.h
        MenuBar.cpp
        MenuBar.h
        MainWindow.cpp
        MainWindow.h
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
        workarea/VGMFileTreeView.cpp
        workarea/VGMFileTreeView.h
        workarea/VGMFileView.cpp
        workarea/VGMFileView.h
        workarea/VGMFileListView.cpp
        workarea/VGMFileListView.h)

target_include_directories(vgmtrans PRIVATE . ./util ./workarea)

target_link_libraries(vgmtrans
        PRIVATE
        g_options
        g_warnings
        vgmtranscore
        Qt5::Widgets
        Qt5::Svg
        qhexview-lib
        gsl-lite
        FluidSynth::FluidSynth)

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(BUNDLE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VGMTrans.app")

    set_target_properties(vgmtrans
            PROPERTIES
            MACOSX_BUNDLE true
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/resources/Info.plist.in"
            OUTPUT_NAME VGMTrans)

    target_sources(vgmtrans PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/resources/appicon.icns"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    
    target_sources(vgmtrans PRIVATE "${CMAKE_SOURCE_DIR}/bin/mame_roms.xml")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/bin/mame_roms.xml"
            PROPERTIES MACOSX_PACKAGE_LOCATION MacOS)
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    # Make sure the executable ends up in the binary directory without any additional folders
    set_target_properties(vgmtrans PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<$<CONFIG:Debug>:>")
    target_sources(vgmtrans PRIVATE "${CMAKE_CURRENT_LIST_DIR}/resources/VGMTrans.rc")

    # Copy ROMs definitions for CPS
    add_custom_command(TARGET vgmtrans POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/bin/mame_roms.xml" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    get_target_property(MOC_EXECUTABLE_LOCATION Qt5::moc IMPORTED_LOCATION)
    get_filename_component(QT_BIN_LOCATION "${MOC_EXECUTABLE_LOCATION}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${QT_BIN_LOCATION}")

    add_custom_command(TARGET vgmtrans POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E env PATH="${QT_BIN_LOCATION}"
        "${WINDEPLOYQT}"
            --plugindir="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/QtPlugins"
            --libdir="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations
            --no-compiler-runtime
            --no-angle
            --no-opengl-sw
            "$<TARGET_FILE:vgmtrans>"
    )

    file(WRITE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/qt.conf" "[Paths]\r\nPlugins = ./QtPlugins")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
    install(TARGETS vgmtrans DESTINATION ${CMAKE_INSTALL_PREFIX})
    install(FILES resources/vgmtrans.png
            DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/512x512/apps")
    install(FILES resources/VGMTrans.desktop
            DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
endif()

set(CPACK_PACKAGE_NAME "vgmtrans")
set(CPACK_PACKAGE_VENDOR "VGMTrans Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A converter for sequenced proprietary videogame music formats")
set(CPACK_RPM_PACKAGE_LICENSE ZLIB)

set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR "ZIP")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES "/build.*/;/\\\\.git/")
list(APPEND CPACK_SOURCE_IGNORE_FILES "${CMAKE_BINARY_DIR}")

include(CPack)

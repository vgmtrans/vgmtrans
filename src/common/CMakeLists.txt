add_subdirectory("${PROJECT_SOURCE_DIR}/lib/gsl-lite" "${CMAKE_CURRENT_BINARY_DIR}/gsl-lite" EXCLUDE_FROM_ALL)

find_package(FluidSynth)
if(FluidSynth_FOUND AND "${FluidSynth_VERSION}" MATCHES "2(.)*")
    message(STATUS "Using FluidSynth ${FluidSynth_VERSION}")
else()
    if(MSVC)
        add_library(fluidsynth STATIC IMPORTED)
        set_target_properties(
                fluidsynth
                PROPERTIES
                IMPORTED_LOCATION
                "${CMAKE_SOURCE_DIR}/externals/win-deps/fluidsynth2-msvc2019-x64/fluidsynth.lib"
        )

        target_include_directories(
                fluidsynth
                INTERFACE
                ${CMAKE_SOURCE_DIR}/externals/win-deps/fluidsynth2-msvc2019-x64/include)

        message(STATUS "Using FluidSynth fallback from Windows externals")
    else()
        message(
                FATAL_ERROR "Failed to locate FluidSynth or library too old")
    endif()
endif()

add_library(vgmtranscommon
        MusicPlayer.cpp
        MusicPlayer.h
)

target_include_directories(vgmtranscommon PUBLIC .)

target_link_libraries(vgmtranscommon
        PRIVATE
        g_options
        g_warnings
        PUBLIC
        FluidSynth::FluidSynth
        gsl-lite)

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

get_target_property(FLUIDSYNTH_DLL_LOCATION FluidSynth::FluidSynth IMPORTED_LOCATION)
add_custom_command(TARGET vgmtranscommon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUIDSYNTH_DLL_LOCATION}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)


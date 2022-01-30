project(BASS C)

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    set(LIBRARY_ARCH_LOCATION "aarch64/")
else()
    set(LIBRARY_ARCH_LOCATION "")
endif()

# BASS library
add_library(BASS SHARED IMPORTED GLOBAL)
target_include_directories(BASS INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(BASS PROPERTIES VERSION "2.4.16.7")

if(APPLE)
    set_target_properties(BASS PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libbass.dylib")
elseif(WIN32)
    set_target_properties(BASS PROPERTIES IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/bass.lib")
    set_target_properties(BASS PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bass.dll")
else()
    set_target_properties(BASS PROPERTIES
                                IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/${LIBRARY_ARCH_LOCATION}libbass.so"
                                IMPORTED_NO_SONAME TRUE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${LIBRARY_ARCH_LOCATION}libbass.so" TYPE LIB)
endif()

add_library(BASS::BASS ALIAS BASS)

# BASSMIDI add-on
add_library(BASSMIDI SHARED IMPORTED GLOBAL)
target_include_directories(BASSMIDI INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(BASSMIDI PROPERTIES VERSION "2.4.13")
target_link_libraries(BASSMIDI INTERFACE BASS)

if(APPLE)
    set_target_properties(BASSMIDI PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libbassmidi.dylib")
elseif(WIN32)
    set_target_properties(BASSMIDI PROPERTIES IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/bassmidi.lib")
    set_target_properties(BASSMIDI PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bassmidi.dll")
else()
    set_target_properties(BASSMIDI PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/${LIBRARY_ARCH_LOCATION}libbassmidi.so"
                                IMPORTED_NO_SONAME TRUE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${LIBRARY_ARCH_LOCATION}libbassmidi.so" TYPE LIB)
endif()

add_library(BASS::MIDI ALIAS BASSMIDI)

message(STATUS "Using BASS 2.4.16.7, BASSMIDI 2.4.13")

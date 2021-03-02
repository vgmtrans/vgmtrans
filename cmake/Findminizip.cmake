# Find the Minizip library
#  minizip_INCLUDE_DIRS - minizip include directory
#  minizip_LIBRARIES    - minizip library file
#  minizip_FOUND        - TRUE if minizip is found
#  minizip_VERSION      - minizip version

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_minizip QUIET minizip)
endif()

if(NOT minizip_INCLUDE_DIRS)
    find_path(minizip_INCLUDE_DIRS NAMES unzip.h PATH_SUFFIXES minizip HINTS ${PC_minizip_INCLUDEDIR})
    set(minizip_INCLUDE_DIRS ${minizip_INCLUDE_DIRS}/minizip CACHE PATH "minizip includes")
endif()

set(minizip_VERSION_STRING "${PC_minizip_VERSION}")
if (NOT minizip_VERSION_STRING AND minizip_INCLUDE_DIRS AND EXISTS "${minizip_INCLUDE_DIRS}/unzip.h")
    file(STRINGS "${minizip_INCLUDE_DIRS}/unzip.h" UNZIP_H REGEX "Version [^,]*")

    string(REGEX REPLACE ".*Version ([0-9]+).*" "\\1" minizip_VERSION_MAJOR "${UNZIP_H}")
    string(REGEX REPLACE ".*Version [0-9]+\\.([0-9]+).*" "\\1" minizip_VERSION_MINOR  "${UNZIP_H}")

    set(minizip_VERSION_STRING "${minizip_VERSION_MAJOR}.${minizip_VERSION_MINOR}")
endif()

if(NOT minizip_LIBRARIES)
    find_library(minizip_LIBRARIES NAMES minizip libminizip HINTS ${PC_minizip_LIBRARIES})
endif()

if(minizip_INCLUDE_DIRS AND minizip_LIBRARIES)
    set(minizip_FOUND TRUE)
endif()

mark_as_advanced(minizip_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(minizip
        FOUND_VAR minizip_FOUND
        REQUIRED_VARS minizip_LIBRARIES minizip_INCLUDE_DIRS
        VERSION_VAR minizip_VERSION_STRING
        )

if(minizip_FOUND)
    if(NOT TARGET minizip)
        add_library(minizip UNKNOWN IMPORTED)
        set_target_properties(minizip PROPERTIES
                IMPORTED_LOCATION "${minizip_LIBRARIES}"
                INTERFACE_INCLUDE_DIRECTORIES "${minizip_INCLUDE_DIRS}")
    endif()
endif()
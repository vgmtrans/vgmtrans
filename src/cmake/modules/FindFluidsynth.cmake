# - Find the fluidsynth library
# This module defines
#  FLUIDSYNTH_INCLUDE_DIR, path to fluidsynth/fluidsynth.h, etc.
#  FLUIDSYNTH_LIBRARIES, the libraries required to use FLUIDSYNTH.
#  FLUIDSYNTH_FOUND, If false, do not try to use FLUIDSYNTH.
# also defined, but not for general use are
# FLUIDSYNTH_fluidsynth_LIBRARY, where to find the FLUIDSYNTH library.
# FLUIDSYNTH_ncurses_LIBRARY, where to find the ncurses library [might not be defined]

# Apple fluidsynth does not support fluidsynth hooks
# So we look for another one by default
IF (APPLE OR FREEBSD)
  FIND_PATH (FLUIDSYNTH_INCLUDE_DIR NAMES fluidsynth.h PATHS
    /usr/include/
    /sw/include
    /opt/local/include
    /opt/include
    /usr/local/include
    NO_DEFAULT_PATH
    )
ENDIF (APPLE OR FREEBSD)
FIND_PATH (FLUIDSYNTH_INCLUDE_DIR NAMES fluidsynth.h)


# Apple fluidsynth does not support fluidsynth hooks
# So we look for another one by default
IF (APPLE OR FREEBSD)
  FIND_LIBRARY (FLUIDSYNTH_fluidsynth_LIBRARY NAMES fluidsynth PATHS
    /usr/lib
    /sw/lib
    /opt/local/lib
    /opt/lib
    /usr/local/lib
    NO_DEFAULT_PATH
    )
ENDIF (APPLE OR FREEBSD)
FIND_LIBRARY (FLUIDSYNTH_fluidsynth_LIBRARY NAMES fluidsynth)

# Sometimes fluidsynth really needs ncurses
IF (APPLE OR FREEBSD)
  FIND_LIBRARY (FLUIDSYNTH_ncurses_LIBRARY NAMES ncurses PATHS
    /usr/lib
    /sw/lib
    /opt/local/lib
    /opt/lib
    /usr/local/lib
    /usr/lib
    NO_DEFAULT_PATH
    )
ENDIF (APPLE OR FREEBSD)
FIND_LIBRARY (FLUIDSYNTH_ncurses_LIBRARY NAMES ncurses)

MARK_AS_ADVANCED (
  FLUIDSYNTH_INCLUDE_DIR
  FLUIDSYNTH_fluidsynth_LIBRARY
  FLUIDSYNTH_ncurses_LIBRARY
  )

SET (FLUIDSYNTH_FOUND "NO" )
IF (FLUIDSYNTH_INCLUDE_DIR)
  IF (FLUIDSYNTH_fluidsynth_LIBRARY)
    SET (FLUIDSYNTH_FOUND "YES" )
    SET (FLUIDSYNTH_LIBRARIES
      ${FLUIDSYNTH_fluidsynth_LIBRARY} 
      )

    # some fluidsynth libraries depend on ncurses
    IF (FLUIDSYNTH_ncurses_LIBRARY)
      SET (FLUIDSYNTH_LIBRARIES ${FLUIDSYNTH_LIBRARIES} ${FLUIDSYNTH_ncurses_LIBRARY})
    ENDIF (FLUIDSYNTH_ncurses_LIBRARY)

  ENDIF (FLUIDSYNTH_fluidsynth_LIBRARY)
ENDIF (FLUIDSYNTH_INCLUDE_DIR)

IF (FLUIDSYNTH_FOUND)
  MESSAGE (STATUS "Found fluidsynth library")
ELSE (FLUIDSYNTH_FOUND)
  IF (FLUIDSYNTH_FIND_REQUIRED)
    MESSAGE (FATAL_ERROR "Could not find fluidsynth -- please give some paths to CMake")
  ENDIF (FLUIDSYNTH_FIND_REQUIRED)
ENDIF (FLUIDSYNTH_FOUND)

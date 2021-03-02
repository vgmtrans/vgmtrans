 # Find the Minizip library
 # Defines:

 #  MINIZIP_INCLUDE_DIRS - minizip include directory
 #  MINIZIP_LIBRARY     - minizip library file
 #  MINIZIP_FOUND       - TRUE if minizip is found

 if (MINIZIP_INCLUDE_DIRS)
     #check cache
     set(MINIZIP_FIND_QUIETLY TRUE)
 endif ()

 if (NOT MINIZIP_INCLUDE_DIRS)
     find_path(MINIZIP_INCLUDE_DIRS NAMES unzip.h PATH_SUFFIXES minizip)
     set(MINIZIP_INCLUDE_DIRS ${MINIZIP_INCLUDE_DIRS}/minizip CACHE PATH "minizip includes")
 endif ()

 find_library(MINIZIP_LIBRARIES NAMES minizip libminizip)

 if (MINIZIP_INCLUDE_DIRS AND MINIZIP_LIBRARIES)
     set(MINIZIP_FOUND TRUE)
 endif ()

 if (MINIZIP_FOUND)
     add_library(minizip UNKNOWN IMPORTED)
     set_target_properties(minizip PROPERTIES
             IMPORTED_LOCATION "${MINIZIP_LIBRARIES}"
             INTERFACE_INCLUDE_DIRECTORIES "${MINIZIP_INCLUDE_DIRS}")
     message(STATUS "Found Minizip library: ${MINIZIP_LIBRARIES}")
 endif ()
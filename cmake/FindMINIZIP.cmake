# VGMTrans (c) 2018
# Licensed under the zlib license
# Check the included LICENSE.txt for details
# #
# FindMINIZIP.cmake
# Locate MiniZip headers and libraries
#  MINIZIP_INCLUDE_DIR - Path to the MiniZip headers
#  MINIZIP_LIBRARIES   - Path to the MiniZip libraries
#  MINIZIP_FOUND       - The system has Minizip

find_path(MINIZIP_INCLUDE_DIR
        NAMES unzip.h
        PATH_SUFFIXES minizip
)

find_library(MINIZIP_LIBRARIES
        NAMES minizip
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MiniZip
        FOUND_VAR MINIZIP_FOUND
        REQUIRED_VARS MINIZIP_LIBRARIES MINIZIP_INCLUDE_DIR
)

mark_as_advanced(
        MINIZIP_LIBRARIES MINIZIP_INCLUDE_DIR
)

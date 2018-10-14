# VGMTrans (c) 2018
# Licensed under the zlib license
# Check the included LICENSE.txt for details
# #
# FindTINYXML.cmake
# Locate TINYXML headers and libraries
#  TINYXML_INCLUDE_DIR - Path to the TINYXML headers
#  TINYXML_LIBRARIES   - Path to the TINYXML libraries
#  TINYXML_FOUND       - The system has TINYXML

find_path(TINYXML_INCLUDE_DIR
        NAMES tinyxml.h
        )

find_library(TINYXML_LIBRARIES
        NAMES tinyxml
        )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TINYXML
        FOUND_VAR TINYXML_FOUND
        REQUIRED_VARS TINYXML_LIBRARIES TINYXML_INCLUDE_DIR
        )

mark_as_advanced(
        TINYXML_LIBRARIES TINYXML_INCLUDE_DIR
)

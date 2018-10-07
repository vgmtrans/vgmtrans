project(tinyxml)

set(GENERIC_LIB_VERSION "2.6.2")
set(GENERIC_LIB_SOVERSION "6")

if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_definitions(-DTIXML_USE_STL)

add_library(tinyxml_static STATIC
            tinyxml.cpp tinyxml.h
			tinystr.cpp tinystr.h
			tinyxmlerror.cpp tinyxmlparser.cpp)
add_library(tinyxml ALIAS tinyxml_static)

target_include_directories(tinyxml_static
PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

set_target_properties(tinyxml_static PROPERTIES
        COMPILE_DEFINITONS "TINYXML_EXPORT"
        VERSION "${GENERIC_LIB_VERSION}"
        SOVERSION "${GENERIC_LIB_SOVERSION}"
		OUTPUT_NAME tinyxml)

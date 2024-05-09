# This script has been modified for VGMTrans to only build the zlib/minizip static libraries

project(zlib C)

set(VERSION "1.3.1")

include(CheckTypeSize)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCSourceCompiles)

check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(stdint.h    HAVE_STDINT_H)
check_include_file(stddef.h    HAVE_STDDEF_H)

#
# Check to see if we have large file support
#
set(CMAKE_REQUIRED_DEFINITIONS -D_LARGEFILE64_SOURCE=1)
check_type_size(off64_t OFF64_T)
if(HAVE_OFF64_T)
   add_definitions(-D_LARGEFILE64_SOURCE=1)
endif()
set(CMAKE_REQUIRED_DEFINITIONS) # clear variable

#
# Check for fseeko
#
check_function_exists(fseeko HAVE_FSEEKO)
if(NOT HAVE_FSEEKO)
    add_definitions(-DNO_FSEEKO)
endif()

#
# Check for unistd.h
#
check_include_file(unistd.h Z_HAVE_UNISTD_H)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

configure_file(	${CMAKE_CURRENT_SOURCE_DIR}/zconf.h.cmakein
		${CMAKE_CURRENT_BINARY_DIR}/zconf.h @ONLY)


#============================================================================
# zlib
#============================================================================

set(ZLIB_PUBLIC_HDRS
    ${CMAKE_CURRENT_BINARY_DIR}/zconf.h
    zlib.h
)
set(ZLIB_PRIVATE_HDRS
    crc32.h
    deflate.h
    gzguts.h
    inffast.h
    inffixed.h
    inflate.h
    inftrees.h
    trees.h
    zutil.h
)
set(ZLIB_SRCS
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    inflate.c
    infback.c
    inftrees.c
    inffast.c
    trees.c
    uncompr.c
    zutil.c
)

add_library(zlibstatic STATIC ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS})
add_library(ZLIB::ZLIB ALIAS zlibstatic)
target_include_directories(zlibstatic PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

#============================================================================
# minizip (VGMTrans build addition)
#============================================================================

set(MINIZIP_SOURCE_PATH contrib/minizip)

set(MINIZIP_HDRS
	${MINIZIP_SOURCE_PATH}/crypt.h
	${MINIZIP_SOURCE_PATH}/ioapi.h
	${MINIZIP_SOURCE_PATH}/mztools.h
	${MINIZIP_SOURCE_PATH}/unzip.h
	${MINIZIP_SOURCE_PATH}/zip.h
)

set(MINIZIP_SRCS
	${MINIZIP_SOURCE_PATH}/ioapi.c
	${MINIZIP_SOURCE_PATH}/mztools.c
	${MINIZIP_SOURCE_PATH}/unzip.c
	${MINIZIP_SOURCE_PATH}/zip.c
)

add_library(minizip STATIC ${MINIZIP_SRCS} ${MINIZIP_HDRS})
target_include_directories(minizip
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/${MINIZIP_SOURCE_PATH}
)
target_link_libraries(minizip PRIVATE ZLIB::ZLIB)

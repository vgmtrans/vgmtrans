project(minizip C)

# Check to see if we have large file support
set(CMAKE_REQUIRED_DEFINITIONS -D_LARGEFILE64_SOURCE=1)

if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

#============================================================================
# minizip
#============================================================================

set(MINIZIP_PUBLIC_HDRS
  crypt.h
  ioapi.h
  mztools.h
  unzip.h
  zip.h
)

set(MINIZIP_SRCS
  ioapi.c
  mztools.c
  unzip.c
  zip.c
)

add_library(minizip STATIC ${MINIZIP_SRCS} ${MINIZIP_PUBLIC_HDRS})

target_include_directories(minizip
PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

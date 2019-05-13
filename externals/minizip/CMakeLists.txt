project(minizip C)

set(SOURCES ioapi.c ioapi.h unzip.c unzip.h)

add_definitions(-DNOUNCRYPT)

if(UNIX)
  add_definitions(-D__USE_FILE_OFFSET64)
  add_definitions(-D__USE_LARGEFILE64)
  add_definitions(-D_LARGEFILE64_SOURCE)
  add_definitions(-D_FILE_OFFSET_BIT=64)
endif()

if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

add_library(minizip STATIC ${SOURCES})
target_include_directories(minizip PUBLIC .)

add_library(MiniZip::minizip ALIAS minizip)

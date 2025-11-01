# Add zlib and minizip dependencies
# Prefer system zlib; otherwise build vendored static
option(USE_SYSTEM_ZLIB "Use system-provided zlib if available" ON)

if (USE_SYSTEM_ZLIB)
  find_package(ZLIB QUIET)
endif()

if (NOT ZLIB_FOUND)
  message(STATUS "System zlib not found; building vendored zlib (static)")
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "[zlib] Enable Zlib Examples as tests"              FORCE)
  set(ZLIB_BUILD_SHARED  OFF CACHE BOOL "[zlib] Enable building zlib shared library"        FORCE)
  set(ZLIB_BUILD_STATIC  ON  CACHE BOOL "[zlib] Enable building zlib static library"        FORCE)
  set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "[zlib] Enable building libminizip contrib library" FORCE)
  set(ZLIB_INSTALL       OFF CACHE BOOL "[zlib] Enable installation of zlib"                FORCE)

  add_subdirectory("${PROJECT_SOURCE_DIR}/lib/zlib"
          "${CMAKE_BINARY_DIR}/lib/zlib"
          EXCLUDE_FROM_ALL)

  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# Add minizip from our vendored zlib
set(MINIZIP_DIR "${PROJECT_SOURCE_DIR}/lib/zlib/contrib/minizip")

add_library(minizip STATIC
        "${MINIZIP_DIR}/ioapi.c"
        "${MINIZIP_DIR}/unzip.c"
)
if (WIN32)
  target_sources(minizip PRIVATE "${MINIZIP_DIR}/iowin32.c")
endif()

target_include_directories(minizip PUBLIC "${MINIZIP_DIR}")
target_link_libraries(minizip PUBLIC ZLIB::ZLIB)

# Add libunarr
message(INFO "Using built-in unarr")
set(ENABLE_7Z         OFF CACHE BOOL "[unarr] Build with 7z support"               FORCE)
set(USE_SYSTEM_BZ2    OFF CACHE BOOL "[unarr] Build with system bzip2 if possible" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "[unarr] Build libunarr as a shared library"  FORCE)
set(USE_SYSTEM_ZLIB   OFF CACHE BOOL "[unarr] Build with system zlib if possible"  FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/unarr" "${CMAKE_BINARY_DIR}/lib/unarr" EXCLUDE_FROM_ALL)

# Add libchdr
if (ZLIB_FOUND)
  set(WITH_SYSTEM_ZLIB ON  CACHE BOOL "[libchdr] Use system provided zlib library" FORCE)
else()
  # triggers our vendored zlibstatic path instead of libchdr's vendored zlib
  set(WITH_SYSTEM_ZLIB OFF CACHE BOOL "[libchdr] Use system provided zlib library" FORCE)
endif()
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/libchdr" "${CMAKE_BINARY_DIR}/lib/libchdr" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
     "${PROJECT_SOURCE_DIR}/src/main/*.h")

add_library(vgmtranscore OBJECT ${MAIN_FILES})

configure_file("version.h.in" "version.h")
target_include_directories(vgmtranscore PUBLIC
        .
        ./components
        ./components/instr
        ./components/matcher
        ./components/seq
        ./conversion
        ./formats
        ./formats/common
        ./io
        ./loaders
        ./util
        ${PROJECT_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
  vgmtranscore
  PUBLIC spdlog::spdlog unarr ZLIB::ZLIB minizip mio chdr-static
)

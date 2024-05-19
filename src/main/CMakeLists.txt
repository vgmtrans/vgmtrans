# Define zlib-ng include directory and library path
set(ZLIBNG_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/lib/zlib-ng" "${CMAKE_BINARY_DIR}/lib/zlib-ng")
set(ZLIBNG_FOUND TRUE)
if(UNIX)
    set(ZLIBNG_LIBRARY "${CMAKE_BINARY_DIR}/lib/zlib-ng/libz-ng.a")
elseif(WIN32)
    set(ZLIBNG_LIBRARY "${CMAKE_BINARY_DIR}/lib/zlib-ng/${CMAKE_STATIC_LIBRARY_PREFIX}zlibstatic-ng$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()

# Configure minizip-ng to use zlib-ng
set(MZ_COMPAT ON)
set(MZ_ZLIB ON)
set(MZ_BZIP2 OFF)
set(MZ_LZMA OFF)
set(MZ_ZSTD OFF)
set(MZ_LIBCOMP OFF)
set(MZ_PKCRYPT OFF)
set(MZ_WZAES OFF)
set(MZ_OPENSSL OFF)
set(MZ_FETCH_LIBS OFF)
set(MZ_FORCE_FETCH_LIBS OFF)
set(MZ_DECOMPRESS_ONLY ON)
set(SKIP_INSTALL_ALL ON)

# Add minizip-ng subdirectory
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/minizip-ng"
                 "${CMAKE_CURRENT_BINARY_DIR}/minizip-ng" EXCLUDE_FROM_ALL)

message(INFO "Using built-in unarr")
set(ENABLE_7Z OFF CACHE BOOL "Build with 7z support" FORCE)
set(USE_SYSTEM_BZ2 OFF CACHE BOOL "Build with system bzip2 if possible" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libunarr as a shared library" FORCE)
set(USE_SYSTEM_ZLIB OFF CACHE BOOL "Build with system zlib if possible" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/unarr"
        "${CMAKE_CURRENT_BINARY_DIR}/unarr" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
     "${PROJECT_SOURCE_DIR}/src/main/*.h")

add_library(vgmtranscore OBJECT ${MAIN_FILES})

configure_file("version.h.in" "version.h")
target_include_directories(vgmtranscore PUBLIC
        .
        ./components
        ./components/instr
        ./components/seq
        ./conversion
        ./formats
        ./formats/common
        ./io
        ./loaders
        ./util
        ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
  vgmtranscore
  PUBLIC spdlog::spdlog unarr zlibstatic minizip tinyxml mio
  PRIVATE g_options g_warnings)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build 3rd-party libraries as shared ones" FORCE)

message(INFO "Using built-in zlib-ng")
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/zlib-ng"
                 "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng" EXCLUDE_FROM_ALL)
add_library(zlib-ng::zlibstatic ALIAS zlib)

# Hack for including the static zlib-ng library (TODO: find a better solution for this)
set(CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng")
set(CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng")
if(UNIX)
  file(TOUCH "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng/libz-ng.a")
else()
  file(TOUCH "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng/zlibstatic-ng.lib")
  file(CREATE_LINK zlibstatic-ng.lib "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng/z-ng.lib" SYMBOLIC)
endif()

message(INFO "Using built-in minizip-ng")
set(MZ_COMPAT ON CACHE BOOL "Enables compatibility layer" FORCE)
set(MZ_BZIP2 OFF CACHE BOOL "Enables BZIP2 compression" FORCE)
set(MZ_ZSTD OFF CACHE BOOL "Enables ZSTD compression" FORCE)
set(MZ_FETCH_LIBS OFF CACHE BOOL "Enables fetching third-party libraries if not found" FORCE)
set(MZ_OPENSSL OFF CACHE BOOL "Enables OpenSSL for encryption" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/minizip-ng"
                 "${CMAKE_CURRENT_BINARY_DIR}/minizip-ng" EXCLUDE_FROM_ALL)
add_library(MINIZIP::minizip ALIAS minizip)

message(INFO "Using built-in unarr")
set(ENABLE_7Z OFF CACHE BOOL "Build with 7z support" FORCE)
set(USE_SYSTEM_BZ2 OFF CACHE BOOL "Build with system bzip2 if possible" FORCE)
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
  PUBLIC spdlog::spdlog zlib-ng::zlibstatic unarr MINIZIP::minizip tinyxml mio
  PRIVATE g_options g_warnings)

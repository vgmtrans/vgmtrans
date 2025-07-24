# Add minizip-ng as a dependency
# On initial set up, minizip-ng decides whether to use zlib-ng (over regular zlib) by searching for the lib, but it
# wouldn't find it because it hasn't been built yet. We trick its CMake script by setting the variables that indicate
# the lib was found with values that will be correct once it is built. zlib-ng will be built before minizip-ng because
# we added it in the root CMakeLists.txt file.

option(USE_VENDORED_ZLIBNG "Use the vendored version of zlib-ng" ON)

if (USE_VENDORED_ZLIBNG)
  #include zlib-ng sans tests
  set(BUILD_SHARED_LIBS OFF)

  set(ZLIB_ENABLE_TESTS OFF)
  set(ZLIBNG_ENABLE_TESTS OFF)
  set(WITH_GTEST OFF)
  add_subdirectory("${PROJECT_SOURCE_DIR}/lib/zlib-ng" "${CMAKE_BINARY_DIR}/lib/zlib-ng" EXCLUDE_FROM_ALL)

  set(ZLIBNG_FOUND TRUE)
  set(ZLIBNG_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/lib/zlib-ng" "${CMAKE_BINARY_DIR}/lib/zlib-ng")
  set(ZLIBNG_LIBRARY $<TARGET_FILE:zlibstatic>)
endif()

# Configure minizip-ng to use zlib and none of the other options
set(MZ_COMPAT ON)
set(MZ_ZLIB ON)
set(MZ_BZIP2 OFF)
set(MZ_LZMA OFF)
set(MZ_ZSTD OFF)
set(MZ_LIBCOMP OFF)
set(MZ_PKCRYPT OFF)
set(MZ_WZAES OFF)
set(MZ_OPENSSL OFF)
set(MZ_DECOMPRESS_ONLY ON)
set(SKIP_INSTALL_ALL ON)

# Add minizip-ng subdirectory
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/minizip-ng" "${CMAKE_BINARY_DIR}/lib/minizip-ng" EXCLUDE_FROM_ALL)

if (USE_VENDORED_ZLIBNG)
  add_dependencies(minizip zlibstatic)
endif()

message(INFO "Using built-in unarr")
set(ENABLE_7Z OFF CACHE BOOL "Build with 7z support" FORCE)
set(USE_SYSTEM_BZ2 OFF CACHE BOOL "Build with system bzip2 if possible" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libunarr as a shared library" FORCE)
set(USE_SYSTEM_ZLIB OFF CACHE BOOL "Build with system zlib if possible" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/unarr" "${CMAKE_BINARY_DIR}/lib/unarr" EXCLUDE_FROM_ALL)

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
        ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
  vgmtranscore
  PUBLIC spdlog::spdlog unarr zlibstatic minizip tinyxml mio chdr-static
)

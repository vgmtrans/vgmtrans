# Add minizip-ng as a dependency
# On initial set up, minizip-ng decides whether to use zlib-ng (over regular zlib) by searching for the lib, but it
# wouldn't find it because it hasn't been built yet. We trick its CMake script by setting the variables that indicate
# the lib was found with values that will be correct once it is built. zlib-ng will be built before minizip-ng because
# we added it in the root CMakeLists.txt file.

option(USE_VENDORED_ZLIBNG "Use the vendored version of zlib-ng" ON)
set(ZLIB_COMPAT ON CACHE BOOL "Compile zlib-ng with zlib compatible API" FORCE)

if (USE_VENDORED_ZLIBNG)
  #include zlib-ng sans tests
  set(BUILD_SHARED_LIBS OFF)

  set(ZLIB_ENABLE_TESTS OFF)
  set(ZLIBNG_ENABLE_TESTS OFF)
  set(WITH_GTEST OFF)
  add_subdirectory("${PROJECT_SOURCE_DIR}/lib/zlib-ng" "${CMAKE_BINARY_DIR}/lib/zlib-ng" EXCLUDE_FROM_ALL)

  if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
  endif()

  set(ZLIB_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/zlib-ng")
  set(ZLIB_INCLUDE_DIRS
          "${PROJECT_SOURCE_DIR}/lib/zlib-ng"
          "${CMAKE_BINARY_DIR}/lib/zlib-ng"
  )

  get_directory_property(ZLIB_VERSION DIRECTORY "${CMAKE_BINARY_DIR}/lib/zlib-ng" DEFINITION PROJECT_VERSION)
  if(NOT ZLIB_VERSION)
    set(ZLIB_VERSION "0")
  endif()
  set(ZLIB_VERSION_STRING "${ZLIB_VERSION}")

  set_target_properties(ZLIB::ZLIB PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}"
          INTERFACE_LINK_LIBRARIES "zlib"
  )

  set(_vgmtrans_zlib_config_dir "${CMAKE_BINARY_DIR}/vgmtrans-zlib")
  configure_file("${PROJECT_SOURCE_DIR}/cmake/zlib/ZLIBConfig.cmake.in"
          "${_vgmtrans_zlib_config_dir}/ZLIBConfig.cmake"
          @ONLY)

  set(ZLIB_FOUND TRUE)
  set(ZLIB_DIR "${_vgmtrans_zlib_config_dir}" CACHE PATH "Path to vendored zlib configuration" FORCE)
  set(ZLIB_LIBRARIES ZLIB::ZLIB)
  set(ZLIB_LIBRARY ZLIB::ZLIB)
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
set(MZ_FETCH_LIBS OFF CACHE BOOL "Disable minizip-ng third-party fetching" FORCE)
set(MZ_FORCE_FETCH_LIBS OFF CACHE BOOL "Disable minizip-ng forced fetching" FORCE)

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

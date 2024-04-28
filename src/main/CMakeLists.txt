find_package(minizip)
if(NOT minizip_FOUND)
  # Use our own, if there's no minizip there's probably also no zlib Also avoid
  # risks of having to link to BZip2 on some platforms
  message(INFO "Using built-in zlib and minizip")
  add_subdirectory("${PROJECT_SOURCE_DIR}/lib/zlib"
                   "${CMAKE_CURRENT_BINARY_DIR}/zlib" EXCLUDE_FROM_ALL)
else()
  find_package(ZLIB)
endif()

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
  PUBLIC spdlog::spdlog ZLIB::ZLIB unarr minizip tinyxml ghc_filesystem mio
  PRIVATE g_options g_warnings)

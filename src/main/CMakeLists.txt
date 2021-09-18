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

file(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
     "${PROJECT_SOURCE_DIR}/src/main/*.h")

add_library(vgmtranscore OBJECT ${MAIN_FILES})

configure_file("version.h.in" "version.h")
target_include_directories(vgmtranscore PUBLIC . ./formats ./loaders
                                               ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
  vgmtranscore
  PUBLIC ZLIB::ZLIB minizip tinyxml ghc_filesystem
  PRIVATE g_options g_warnings)

file(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
                  "${PROJECT_SOURCE_DIR}/src/main/*.h")

add_library(vgmcore OBJECT ${MAIN_FILES})

target_include_directories(vgmcore PUBLIC . ./formats ./loaders)
target_link_libraries(vgmcore
                      PRIVATE g_options
                              g_warnings
                              ZLIB::ZLIB
                              minizip
                              tinyxml)

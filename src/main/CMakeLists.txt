file(GLOB_RECURSE
     MAIN_FILES
     "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
     "${PROJECT_SOURCE_DIR}/src/main/*.h")

add_library(vgmcore OBJECT ${MAIN_FILES})

target_include_directories(vgmcore PUBLIC . ./formats ./loaders)
#fixme: this stuff shouldn't be public
target_link_libraries(vgmcore
                      PUBLIC g_options
                             g_warnings
                             fmt::fmt
                             ZLIB::ZLIB
                             minizip
                             tinyxml)

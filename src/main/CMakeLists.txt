find_package(ZLIB)

file(GLOB_RECURSE
        MAIN_FILES
        "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/main/*.h")

add_library(vgmtranscore OBJECT ${MAIN_FILES})

target_include_directories(vgmtranscore PUBLIC . ./formats ./loaders)
target_link_libraries(vgmtranscore
        PUBLIC
        ZLIB::ZLIB
        minizip
        tinyxml
        PRIVATE
        g_options
        g_warnings)
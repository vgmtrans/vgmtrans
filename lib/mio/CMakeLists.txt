add_library(mio INTERFACE)
target_include_directories(mio INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
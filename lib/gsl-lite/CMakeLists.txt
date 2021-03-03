add_library(gsl-lite INTERFACE)
target_include_directories(gsl-lite INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
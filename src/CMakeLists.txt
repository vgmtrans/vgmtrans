add_subdirectory(main)

if(ENABLE_UI_QT)
  message(STATUS "Building UI (Qt)")
  add_subdirectory(ui/qt)
endif()

# Temporary hack. Add libchdr here instead of src/main/CMakeLists.txt to solve a custom target name conflict
# fluidsynth and zstd both define the target 'uninstall', but zstd gracefully handles if it's already defined,
# fluidsynth does not. We therefore must have it defined by fluidsynth first.
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/libchdr" "${CMAKE_BINARY_DIR}/lib/libchdr" EXCLUDE_FROM_ALL)

if(ENABLE_CLI)
  message(STATUS "Building CLI application")
  add_subdirectory(ui/cli)
endif()
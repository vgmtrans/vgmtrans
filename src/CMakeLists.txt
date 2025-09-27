add_subdirectory(main)

if(ENABLE_UI_QT)
  message(STATUS "Building UI (Qt)")
  add_subdirectory(ui/qt)
endif()

if(NOT USE_SYSTEM_LIBCHDR)
  # Add vendored libchdr here so fluidsynth claims the shared uninstall target first.
  set(WITH_SYSTEM_ZLIB ${USE_SYSTEM_ZLIB} CACHE BOOL "[libchdr] Use system provided zlib library" FORCE)
  add_subdirectory("${PROJECT_SOURCE_DIR}/lib/libchdr" "${CMAKE_BINARY_DIR}/lib/libchdr" EXCLUDE_FROM_ALL)
endif()

if(ENABLE_SHELL)
  message(STATUS "Building shell application")
  add_subdirectory(ui/shell)
endif()

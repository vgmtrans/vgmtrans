add_subdirectory(main)

if(ENABLE_UI_QT)
  message(STATUS "Building UI (Qt)")
  add_subdirectory(ui/qt)
endif()

if(ENABLE_CLI)
  message(STATUS "Building CLI application")
  add_subdirectory(ui/cli)
endif()

if(ENABLE_SHELL)
  message(STATUS "Building shell application")
  add_subdirectory(ui/shell)
endif()

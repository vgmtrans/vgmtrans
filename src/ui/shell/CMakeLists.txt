add_executable(vgmtrans-shell)
vgmtrans_enable_project_warnings(vgmtrans-shell)
target_sources(vgmtrans-shell
  PRIVATE
    vgmtrans-shell.cpp
    DBGVGMRoot.cpp
    linenoise.c
    commands.cpp
)

target_include_directories(vgmtrans-shell PUBLIC "${PROJECT_BINARY_DIR}/src")
target_link_libraries(vgmtrans-shell PRIVATE vgmtranscore)
target_compile_features(vgmtrans-shell PRIVATE cxx_std_20)

if(INSTALL_SHELL)
  install(TARGETS vgmtrans-shell
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endif()

# The command layer has no terminal or legacy dependencies and is exercised
# directly by the shell integration tests.
add_library(vgmtransshell STATIC commands.cpp ExportOptions.cpp)
vgmtrans_enable_project_warnings(vgmtransshell)
target_include_directories(vgmtransshell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vgmtransshell PUBLIC vgmtransvaluecore PRIVATE fmt::fmt)

add_executable(vgmtrans-shell)
vgmtrans_enable_project_warnings(vgmtrans-shell)
target_sources(vgmtrans-shell
  PRIVATE
    vgmtrans-shell.cpp
    linenoise.c
)

target_link_libraries(vgmtrans-shell PRIVATE vgmtransshell vgmtransvalueformats)
target_compile_features(vgmtrans-shell PRIVATE cxx_std_20)

if(INSTALL_SHELL)
  install(TARGETS vgmtrans-shell
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endif()

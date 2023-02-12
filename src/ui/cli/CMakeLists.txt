add_executable(
  vgmtrans-cli
  CLIVGMRoot.cpp
  main_cli.cpp
)

target_link_libraries(
  vgmtrans-cli
  PRIVATE g_options
          g_warnings
          vgmtranscore)

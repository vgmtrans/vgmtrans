add_executable(
  vgmtrans-cli
  CLIVGMRoot.cpp
  main_cli.cpp
)

target_include_directories(vgmtrans-cli PUBLIC "${PROJECT_BINARY_DIR}/src")

target_link_libraries(
  vgmtrans-cli
  PRIVATE g_options
          g_warnings
          vgmtranscore)

add_executable(
  vgmtrans-cli
  main_cli.cpp
)

target_include_directories(vgmtrans-cli PRIVATE . ./util ./workarea)

target_link_libraries(
  vgmtrans-cli
  PRIVATE g_options
          g_warnings
          vgmtranscore)

set_target_properties(
    vgmtrans-cli
    PROPERTIES
        OUTPUT_NAME vgmtrans)

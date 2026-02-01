add_executable(
  vgmtrans-shell
  vgmtrans-shell.cpp
  DBGVGMRoot.cpp
  linenoise.c
  commands.cpp
)

target_include_directories(vgmtrans-shell PUBLIC "${PROJECT_BINARY_DIR}/src")
target_link_libraries(vgmtrans-shell PRIVATE vgmtranscore fmt::fmt)

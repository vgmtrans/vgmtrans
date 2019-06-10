add_executable(vgmtrans_test
    file_io.cpp)

target_link_libraries(vgmtrans_test PRIVATE vgmcore catch)
target_include_directories(vgmtrans_test PRIVATE . ../src/main)

add_test(vgmtrans_test vgmtrans_test)
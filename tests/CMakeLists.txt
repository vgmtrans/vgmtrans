add_executable(vgmtrans_test
	main.cpp
	file_io.cpp)

target_link_libraries(vgmtrans_test PRIVATE vgmcore catch g_options g_warnings)
target_include_directories(vgmtrans_test PRIVATE . ../src/main)

add_test(vgmtrans_test vgmtrans_test)

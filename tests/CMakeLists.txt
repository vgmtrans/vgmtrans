add_executable(vgmcis_test
	main.cpp
	file_io.cpp
	loader.cpp
	psf.cpp)

target_link_libraries(vgmcis_test PRIVATE vgmcore catch g_options g_warnings)
target_include_directories(vgmcis_test PRIVATE . ../src/main)

add_test(vgmcis_test vgmcis_test)

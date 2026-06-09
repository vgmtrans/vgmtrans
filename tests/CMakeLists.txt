add_executable(vgmtrans-value-core-tests)
target_sources(vgmtrans-value-core-tests
  PRIVATE
    formats/CapcomSnesModuleTests.cpp
    core/ValueCoreTests.cpp
)
target_link_libraries(vgmtrans-value-core-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-value-core-tests PRIVATE cxx_std_20)

add_test(NAME vgmtrans-value-core-tests COMMAND vgmtrans-value-core-tests)

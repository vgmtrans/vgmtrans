add_executable(vgmtrans-value-core-tests)
target_sources(vgmtrans-value-core-tests
  PRIVATE
    formats/CapcomSnesModuleTests.cpp
    formats/NdsModuleTests.cpp
    core/ValueCoreTests.cpp
)
target_link_libraries(vgmtrans-value-core-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-value-core-tests PRIVATE cxx_std_20)

add_test(NAME vgmtrans-value-core-tests COMMAND vgmtrans-value-core-tests)

add_executable(vgmtrans-parity)
target_sources(vgmtrans-parity
  PRIVATE
    parity/vgmtrans-parity.cpp
)
target_link_libraries(vgmtrans-parity PRIVATE vgmtranscore)
target_compile_features(vgmtrans-parity PRIVATE cxx_std_20)

add_test(NAME vgmtrans-parity-self-test COMMAND vgmtrans-parity --self-test)

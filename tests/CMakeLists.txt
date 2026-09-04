add_executable(vgmtrans-value-core-tests)
target_sources(vgmtrans-value-core-tests
  PRIVATE
    formats/AsciiShuichiSnesModuleTests.cpp
    formats/CapcomSnesModuleTests.cpp
    formats/CompileSnesModuleTests.cpp
    formats/FalcomSnesModuleTests.cpp
    formats/GraphResSnesModuleTests.cpp
    formats/HeartBeatPS1ModuleTests.cpp
    formats/HeartBeatSnesModuleTests.cpp
    formats/HudsonSnesModuleTests.cpp
    formats/ItikitiSnesModuleTests.cpp
    formats/MoriSnesModuleTests.cpp
    formats/CpsModuleTests.cpp
    formats/KonamiArcadeModuleTests.cpp
    formats/KonamiPS1ModuleTests.cpp
    formats/KonamiTMNT2ModuleTests.cpp
    formats/KonamiSnesModuleTests.cpp
    formats/Mp2kModuleTests.cpp
    formats/NamcoSnesModuleTests.cpp
    formats/NeverlandSnesModuleTests.cpp
    formats/NinSnesModuleTests.cpp
    formats/HOSAModuleTests.cpp
    formats/PandoraBoxSnesModuleTests.cpp
    formats/PrismSnesModuleTests.cpp
    formats/RareSnesModuleTests.cpp
    formats/AkaoSnesModuleTests.cpp
    formats/AkaoFormatTests.cpp
    formats/NdsModuleTests.cpp
    formats/SegSatModuleTests.cpp
    formats/SonyPS1ModuleTests.cpp
    formats/SoftCreatSnesModuleTests.cpp
    formats/SuzukiPS1ModuleTests.cpp
    formats/SuzukiSnesModuleTests.cpp
    formats/TamsoftPS1ModuleTests.cpp
    formats/TriAcePS1ModuleTests.cpp
    formats/WolfTeamSnesModuleTests.cpp
    core/SessionSnapshotBuilder.h
    core/ValueCollectionDiscoveryTests.cpp
    core/ValueCollectionStitchTests.cpp
    core/ValueCompilerCursorTests.cpp
    core/ValueInstrumentVariantTests.cpp
    core/ValueMidiTests.cpp
    core/ValuePsxTests.cpp
    core/ValueRegistryTests.cpp
    core/ValueSequenceModelTests.cpp
    core/ValueSequenceVmTests.cpp
    core/ValueSessionTests.cpp
    core/ValueSourceMapTests.cpp
    core/SampleFilteringTests.cpp
    core/ValueSynthBuilderTests.cpp
    core/ValueSynthExportTests.cpp
    core/ValueCoreTests.cpp
)
target_link_libraries(vgmtrans-value-core-tests PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
target_compile_features(vgmtrans-value-core-tests PRIVATE cxx_std_20)

add_test(NAME vgmtrans-value-core-tests COMMAND vgmtrans-value-core-tests)

add_executable(vgmtrans-konami-ps1-tests
  formats/KonamiPS1ModuleTests.cpp
  formats/KonamiPS1TestMain.cpp)
target_link_libraries(vgmtrans-konami-ps1-tests PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
target_compile_features(vgmtrans-konami-ps1-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-konami-ps1-tests COMMAND vgmtrans-konami-ps1-tests)

add_executable(vgmtrans-hosa-tests
  formats/HOSAModuleTests.cpp
  formats/HOSATestMain.cpp)
target_link_libraries(vgmtrans-hosa-tests PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
target_compile_features(vgmtrans-hosa-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-hosa-tests COMMAND vgmtrans-hosa-tests)

add_executable(vgmtrans-namco-snes-tests
  formats/NamcoSnesModuleTests.cpp
  formats/NamcoSnesTestMain.cpp)
target_link_libraries(vgmtrans-namco-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-namco-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-namco-snes-tests COMMAND vgmtrans-namco-snes-tests)

add_executable(vgmtrans-square-ps2-tests formats/SquarePS2ModuleTests.cpp)
target_link_libraries(vgmtrans-square-ps2-tests PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
target_compile_features(vgmtrans-square-ps2-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-square-ps2-tests COMMAND vgmtrans-square-ps2-tests)

add_executable(vgmtrans-sony-ps2-tests formats/SonyPS2ModuleTests.cpp)
target_link_libraries(vgmtrans-sony-ps2-tests PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
target_compile_features(vgmtrans-sony-ps2-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-sony-ps2-tests COMMAND vgmtrans-sony-ps2-tests)

add_executable(vgmtrans-compile-snes-tests
  formats/CompileSnesModuleTests.cpp
  formats/CompileSnesTestMain.cpp)
target_link_libraries(vgmtrans-compile-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-compile-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-compile-snes-tests COMMAND vgmtrans-compile-snes-tests)

add_executable(vgmtrans-mp2k-tests
  formats/Mp2kModuleTests.cpp
  formats/Mp2kTestMain.cpp)
target_link_libraries(vgmtrans-mp2k-tests PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
target_compile_features(vgmtrans-mp2k-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-mp2k-tests COMMAND vgmtrans-mp2k-tests)

add_executable(vgmtrans-hudson-snes-tests
  formats/HudsonSnesModuleTests.cpp
  formats/HudsonSnesTestMain.cpp
)
target_link_libraries(vgmtrans-hudson-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-hudson-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-hudson-snes-tests COMMAND vgmtrans-hudson-snes-tests)

add_executable(vgmtrans-heartbeat-snes-tests
  formats/HeartBeatSnesModuleTests.cpp
  formats/HeartBeatSnesTestMain.cpp
)
target_link_libraries(vgmtrans-heartbeat-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-heartbeat-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-heartbeat-snes-tests COMMAND vgmtrans-heartbeat-snes-tests)

add_executable(vgmtrans-ascii-shuichi-snes-tests
  formats/AsciiShuichiSnesModuleTests.cpp
  formats/AsciiShuichiSnesTestMain.cpp
)
target_link_libraries(vgmtrans-ascii-shuichi-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-ascii-shuichi-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-ascii-shuichi-snes-tests COMMAND vgmtrans-ascii-shuichi-snes-tests)

add_executable(vgmtrans-prism-snes-tests
  formats/PrismSnesModuleTests.cpp
  formats/PrismSnesTestMain.cpp
)
target_link_libraries(vgmtrans-prism-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-prism-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-prism-snes-tests COMMAND vgmtrans-prism-snes-tests)

add_executable(vgmtrans-falcom-snes-tests
  formats/FalcomSnesModuleTests.cpp
  formats/FalcomSnesTestMain.cpp
)
target_link_libraries(vgmtrans-falcom-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-falcom-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-falcom-snes-tests COMMAND vgmtrans-falcom-snes-tests)

add_executable(vgmtrans-chun-snes-tests
  formats/ChunSnesModuleTests.cpp
  formats/ChunSnesTestMain.cpp
)
target_link_libraries(vgmtrans-chun-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-chun-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-chun-snes-tests COMMAND vgmtrans-chun-snes-tests)

add_executable(vgmtrans-wolf-team-snes-tests
  formats/WolfTeamSnesModuleTests.cpp
  formats/WolfTeamSnesTestMain.cpp
)
target_link_libraries(vgmtrans-wolf-team-snes-tests PRIVATE vgmtransvalueformats vgmtransvaluecore)
target_compile_features(vgmtrans-wolf-team-snes-tests PRIVATE cxx_std_20)
add_test(NAME vgmtrans-wolf-team-snes-tests COMMAND vgmtrans-wolf-team-snes-tests)

if(ENABLE_UI_QT)
  add_executable(vgmtrans-value-ui-tests ui/ValueUiModelTests.cpp)
  target_link_libraries(vgmtrans-value-ui-tests PRIVATE vgmtransqtvalue)
  target_compile_features(vgmtrans-value-ui-tests PRIVATE cxx_std_20)
  add_test(NAME vgmtrans-value-ui-tests COMMAND vgmtrans-value-ui-tests)
endif()

add_executable(vgmtrans-parity)
target_sources(vgmtrans-parity
  PRIVATE
    parity/vgmtrans-parity.cpp
)
target_link_libraries(vgmtrans-parity PRIVATE vgmtranscore)
target_compile_features(vgmtrans-parity PRIVATE cxx_std_20)

add_test(NAME vgmtrans-parity-self-test COMMAND vgmtrans-parity --self-test)
set_tests_properties(vgmtrans-parity-self-test PROPERTIES LABELS "parity;self-test")

# Real game files cannot live in the repository. Point these cache entries at a
# local corpus to register focused CTest cases. Each case compares the original
# VGMRoot/VGMSeq path with the rewritten value path in the same executable.
set(VGMTRANS_PARITY_CAPCOM_SNES_FILE "" CACHE FILEPATH "Capcom SNES RSN file used for original/value parity")
set(VGMTRANS_PARITY_AKAO_SNES_FILE "" CACHE FILEPATH "Akao SNES RSN or SPC file used for original/value parity")
set(VGMTRANS_PARITY_KONAMI_SNES_FILE "" CACHE FILEPATH "Konami SNES RSN or SPC file used for original/value parity")
set(VGMTRANS_PARITY_NIN_SNES_FILES "" CACHE STRING
    "Semicolon-separated NinSnes RSN or SPC files used for original/value parity")
set(VGMTRANS_PARITY_KONAMI_ARCADE_FILE "" CACHE FILEPATH "Konami Arcade MAME ZIP file used for original/value parity")
set(VGMTRANS_PARITY_CPS_FILES "" CACHE STRING "Semicolon-separated CPS1/CPS2/CPS3 MAME ZIP files used for parity")
set(VGMTRANS_PARITY_AKAO_FILE "" CACHE FILEPATH "Akao PSF or raw file used for original/value parity")
set(VGMTRANS_PARITY_NDS_FILE "" CACHE FILEPATH "NDS or 2SF file used for original/value parity")
set(VGMTRANS_PARITY_SEGSAT_FILES "" CACHE STRING "Semicolon-separated SSF or raw Saturn files used for parity")

function(add_vgmtrans_parity_corpus_test test_name family mode corpus_file)
  add_test(
    NAME "vgmtrans-parity-${test_name}"
    COMMAND $<TARGET_FILE:vgmtrans-parity> "${mode}" "${corpus_file}")
  set_tests_properties(
    "vgmtrans-parity-${test_name}"
    PROPERTIES LABELS "parity;corpus;${family}")
endfunction()

if(VGMTRANS_PARITY_CAPCOM_SNES_FILE)
  add_vgmtrans_parity_corpus_test(
    capcom-snes-aram-summary capcom-snes capcom-snes-rsn-summary "${VGMTRANS_PARITY_CAPCOM_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    capcom-snes-aram-midi capcom-snes capcom-snes-rsn-midi "${VGMTRANS_PARITY_CAPCOM_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    capcom-snes-direct-summary capcom-snes capcom-snes-rsn-direct-summary "${VGMTRANS_PARITY_CAPCOM_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    capcom-snes-direct-midi capcom-snes capcom-snes-rsn-direct-midi "${VGMTRANS_PARITY_CAPCOM_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    capcom-snes-direct-synth capcom-snes capcom-snes-rsn-direct-synth "${VGMTRANS_PARITY_CAPCOM_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    capcom-snes-direct-export capcom-snes capcom-snes-rsn-direct-export "${VGMTRANS_PARITY_CAPCOM_SNES_FILE}")
endif()

if(VGMTRANS_PARITY_AKAO_SNES_FILE)
  add_vgmtrans_parity_corpus_test(
    akao-snes-summary akao-snes akao-snes-direct-summary "${VGMTRANS_PARITY_AKAO_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    akao-snes-midi akao-snes akao-snes-direct-midi "${VGMTRANS_PARITY_AKAO_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    akao-snes-synth akao-snes akao-snes-direct-synth "${VGMTRANS_PARITY_AKAO_SNES_FILE}")
endif()

if(VGMTRANS_PARITY_KONAMI_SNES_FILE)
  add_vgmtrans_parity_corpus_test(
    konami-snes-summary konami-snes konami-snes-direct-summary "${VGMTRANS_PARITY_KONAMI_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    konami-snes-midi konami-snes konami-snes-direct-midi "${VGMTRANS_PARITY_KONAMI_SNES_FILE}")
  add_vgmtrans_parity_corpus_test(
    konami-snes-synth konami-snes konami-snes-direct-synth "${VGMTRANS_PARITY_KONAMI_SNES_FILE}")
endif()

set(nin_snes_parity_index 0)
foreach(nin_snes_file IN LISTS VGMTRANS_PARITY_NIN_SNES_FILES)
  math(EXPR nin_snes_parity_index "${nin_snes_parity_index} + 1")
  add_vgmtrans_parity_corpus_test(
    "nin-snes-${nin_snes_parity_index}-summary" nin-snes nin-snes-direct-summary "${nin_snes_file}")
  add_vgmtrans_parity_corpus_test(
    "nin-snes-${nin_snes_parity_index}-midi" nin-snes nin-snes-direct-midi "${nin_snes_file}")
  add_vgmtrans_parity_corpus_test(
    "nin-snes-${nin_snes_parity_index}-synth" nin-snes nin-snes-direct-synth "${nin_snes_file}")
endforeach()

if(VGMTRANS_PARITY_KONAMI_ARCADE_FILE)
  add_vgmtrans_parity_corpus_test(
    konami-arcade-summary konami-arcade konami-arcade-direct-summary "${VGMTRANS_PARITY_KONAMI_ARCADE_FILE}")
endif()

set(cps_parity_index 0)
foreach(cps_file IN LISTS VGMTRANS_PARITY_CPS_FILES)
  math(EXPR cps_parity_index "${cps_parity_index} + 1")
  add_vgmtrans_parity_corpus_test(
    "cps-${cps_parity_index}-summary" cps cps-direct-summary "${cps_file}")
  add_vgmtrans_parity_corpus_test(
    "cps-${cps_parity_index}-midi" cps cps-direct-midi "${cps_file}")
  add_vgmtrans_parity_corpus_test(
    "cps-${cps_parity_index}-synth" cps cps-direct-synth "${cps_file}")
endforeach()

if(VGMTRANS_PARITY_AKAO_FILE)
  add_vgmtrans_parity_corpus_test(
    akao-summary akao akao-direct-summary "${VGMTRANS_PARITY_AKAO_FILE}")
  add_vgmtrans_parity_corpus_test(
    akao-midi akao akao-direct-midi "${VGMTRANS_PARITY_AKAO_FILE}")
  add_vgmtrans_parity_corpus_test(
    akao-synth akao akao-direct-synth "${VGMTRANS_PARITY_AKAO_FILE}")
endif()

if(VGMTRANS_PARITY_NDS_FILE)
  add_vgmtrans_parity_corpus_test(
    nds-summary nds nds-direct-summary "${VGMTRANS_PARITY_NDS_FILE}")
  add_vgmtrans_parity_corpus_test(
    nds-midi nds nds-direct-midi "${VGMTRANS_PARITY_NDS_FILE}")
  add_vgmtrans_parity_corpus_test(
    nds-synth nds nds-direct-synth "${VGMTRANS_PARITY_NDS_FILE}")
endif()

set(segsat_parity_index 0)
foreach(segsat_file IN LISTS VGMTRANS_PARITY_SEGSAT_FILES)
  math(EXPR segsat_parity_index "${segsat_parity_index} + 1")
  add_vgmtrans_parity_corpus_test(
    "segsat-${segsat_parity_index}-summary" segsat segsat-direct-summary "${segsat_file}")
  add_vgmtrans_parity_corpus_test(
    "segsat-${segsat_parity_index}-midi" segsat segsat-direct-midi "${segsat_file}")
  add_vgmtrans_parity_corpus_test(
    "segsat-${segsat_parity_index}-synth" segsat segsat-direct-synth "${segsat_file}")

  # The legacy comparison path is not stable when multiple SSF corpus processes
  # run concurrently. Keep these optional tests isolated so parallel CTest runs
  # remain deterministic.
  set_tests_properties(
    "vgmtrans-parity-segsat-${segsat_parity_index}-summary"
    "vgmtrans-parity-segsat-${segsat_parity_index}-midi"
    "vgmtrans-parity-segsat-${segsat_parity_index}-synth"
    PROPERTIES RUN_SERIAL TRUE)
endforeach()

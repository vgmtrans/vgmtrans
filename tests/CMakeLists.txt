if(ENABLE_SHELL)
  add_executable(vgmtrans-shell-tests shell/ShellTests.cpp)
  target_link_libraries(vgmtrans-shell-tests PRIVATE vgmtransshell)
  target_compile_features(vgmtrans-shell-tests PRIVATE cxx_std_20)
  add_test(NAME vgmtrans-shell-tests COMMAND vgmtrans-shell-tests)
  add_test(NAME vgmtrans-shell-cli-tests
    COMMAND ${CMAKE_COMMAND} -DSHELL_EXECUTABLE=$<TARGET_FILE:vgmtrans-shell>
            -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/shell-cli
            -P ${CMAKE_CURRENT_SOURCE_DIR}/shell/ShellCliTests.cmake)
endif()

# Format regressions run once, in their own executables. The core suite only
# contains architecture tests, so a format failure does not hide unrelated tests.
function(add_value_test target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE vgmtransvalueformats vgmtransvaluecore ZLIB::ZLIB)
  target_compile_features(${target} PRIVATE cxx_std_20)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

function(add_value_format_test target source suite)
  add_value_test(${target} ${source} TestMain.cpp)
  target_compile_definitions(${target} PRIVATE VGMTRANS_TEST_SUITE=${suite})
endfunction()

add_value_test(vgmtrans-value-core-tests
  core/ValueAssetResolutionTests.cpp
  core/ValueCollectionStitchTests.cpp
  core/ValueCompilerCursorTests.cpp
  core/ValueInstrumentVariantTests.cpp
  core/ValueMidiEncodingTests.cpp
  core/ValueMidiRendererTests.cpp
  core/ValueMidiPitchTests.cpp
  core/ValueMidiModulationTests.cpp
  core/ValueSequenceExportTests.cpp
  core/ValueModulationTests.cpp
  core/ValuePsxTests.cpp
  core/ValueRegistryTests.cpp
  core/ValueSequenceModelTests.cpp
  core/ValueSequenceVmTests.cpp
  core/ValueSessionTests.cpp
  core/ValueSourceMapTests.cpp
  core/ValueSampleTests.cpp
  core/ValueSynthBuilderTests.cpp
  core/ValueSynthExportTests.cpp
  core/ValueCollectionExportTests.cpp
  core/ValueCoreTests.cpp
)

add_value_test(vgmtrans-cue-tests core/ValueCueTests.cpp)

add_value_format_test(vgmtrans-akao-tests
  formats/AkaoFormatTests.cpp runAkaoFormatTests)
add_value_format_test(vgmtrans-akao-snes-tests
  formats/AkaoSnesModuleTests.cpp runAkaoSnesModuleTests)
add_value_test(vgmtrans-ascii-shuichi-snes-tests
  formats/AsciiShuichiSnesModuleTests.cpp formats/AsciiShuichiSnesTestMain.cpp)
add_value_format_test(vgmtrans-capcom-snes-tests
  formats/CapcomSnesModuleTests.cpp runCapcomSnesModuleTests)
add_value_test(vgmtrans-chun-snes-tests
  formats/ChunSnesModuleTests.cpp formats/ChunSnesTestMain.cpp)
add_value_format_test(vgmtrans-compile-snes-tests
  formats/CompileSnesModuleTests.cpp runCompileSnesModuleTests)
add_value_format_test(vgmtrans-cps-tests
  formats/CpsModuleTests.cpp runCpsModuleTests)
add_value_test(vgmtrans-falcom-snes-tests
  formats/FalcomSnesModuleTests.cpp formats/FalcomSnesTestMain.cpp)
add_value_format_test(vgmtrans-graph-res-snes-tests
  formats/GraphResSnesModuleTests.cpp runGraphResSnesModuleTests)
add_value_test(vgmtrans-hosa-tests
  formats/HOSAModuleTests.cpp formats/HOSATestMain.cpp)
add_value_format_test(vgmtrans-heart-beat-ps1-tests
  formats/HeartBeatPS1ModuleTests.cpp runHeartBeatPs1ModuleTests)
add_value_test(vgmtrans-heartbeat-snes-tests
  formats/HeartBeatSnesModuleTests.cpp formats/HeartBeatSnesTestMain.cpp)
add_value_format_test(vgmtrans-hudson-snes-tests
  formats/HudsonSnesModuleTests.cpp runHudsonSnesModuleTests)
add_value_format_test(vgmtrans-itikiti-snes-tests
  formats/ItikitiSnesModuleTests.cpp runItikitiSnesModuleTests)
add_value_format_test(vgmtrans-konami-arcade-tests
  formats/KonamiArcadeModuleTests.cpp runKonamiArcadeModuleTests)
add_value_test(vgmtrans-konami-ps1-tests
  formats/KonamiPS1ModuleTests.cpp formats/KonamiPS1TestMain.cpp)
add_value_format_test(vgmtrans-konami-snes-tests
  formats/KonamiSnesModuleTests.cpp runKonamiSnesModuleTests)
add_value_format_test(vgmtrans-konami-tmnt2-tests
  formats/KonamiTMNT2ModuleTests.cpp runKonamiTMNT2ModuleTests)
add_value_format_test(vgmtrans-mori-snes-tests
  formats/MoriSnesModuleTests.cpp runMoriSnesModuleTests)
add_value_test(vgmtrans-mp2k-tests
  formats/Mp2kModuleTests.cpp formats/Mp2kTestMain.cpp)
add_value_test(vgmtrans-namco-snes-tests
  formats/NamcoSnesModuleTests.cpp formats/NamcoSnesTestMain.cpp)
add_value_format_test(vgmtrans-nds-tests
  formats/NdsModuleTests.cpp runNdsModuleTests)
add_value_format_test(vgmtrans-neverland-snes-tests
  formats/NeverlandSnesModuleTests.cpp runNeverlandSnesModuleTests)
add_value_test(vgmtrans-nin-snes-tests
  formats/NinSnesModuleTests.cpp formats/NinSnesTestMain.cpp)
add_value_format_test(vgmtrans-pandora-box-snes-tests
  formats/PandoraBoxSnesModuleTests.cpp runPandoraBoxSnesModuleTests)
add_value_format_test(vgmtrans-prism-snes-tests
  formats/PrismSnesModuleTests.cpp runPrismSnesModuleTests)
add_value_format_test(vgmtrans-rare-snes-tests
  formats/RareSnesModuleTests.cpp runRareSnesModuleTests)
add_value_test(vgmtrans-sculpt-soft-snes-tests
  formats/SculptSoftSnesModuleTests.cpp formats/SculptSoftSnesTestMain.cpp)
add_value_format_test(vgmtrans-seg-sat-tests
  formats/SegSatModuleTests.cpp runSegSatModuleTests)
add_value_format_test(vgmtrans-soft-creat-snes-tests
  formats/SoftCreatSnesModuleTests.cpp runSoftCreatSnesModuleTests)
add_value_format_test(vgmtrans-sony-ps1-tests
  formats/SonyPS1ModuleTests.cpp runSonyPS1ModuleTests)
add_value_test(vgmtrans-sony-ps2-tests formats/SonyPS2ModuleTests.cpp)
add_value_test(vgmtrans-square-ps2-tests formats/SquarePS2ModuleTests.cpp)
add_value_format_test(vgmtrans-suzuki-ps1-tests
  formats/SuzukiPS1ModuleTests.cpp runSuzukiPS1ModuleTests)
add_value_format_test(vgmtrans-suzuki-snes-tests
  formats/SuzukiSnesModuleTests.cpp runSuzukiSnesModuleTests)
add_value_format_test(vgmtrans-tamsoft-ps1-tests
  formats/TamsoftPS1ModuleTests.cpp runTamsoftPs1ModuleTests)
add_value_format_test(vgmtrans-tri-ace-ps1-tests
  formats/TriAcePS1ModuleTests.cpp runTriAcePs1ModuleTests)
add_value_test(vgmtrans-wolf-team-snes-tests
  formats/WolfTeamSnesModuleTests.cpp formats/WolfTeamSnesTestMain.cpp)

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

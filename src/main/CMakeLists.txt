# ~~~
# Dependencies
# ~~~

# mio is a header-only library
add_subdirectory(${PROJECT_SOURCE_DIR}/lib/mio ${CMAKE_CURRENT_BINARY_DIR}/lib/mio EXCLUDE_FROM_ALL)

# nhlohmann-json is a header-only library,
# but some distros provide it as a system package
if(USE_SYSTEM_NLOHMANN)
  find_package(nlohmann_json 3.12.0 REQUIRED)
else()
  message(STATUS "Using vendored nlohmann_json library")
  add_library(nlohmann_json INTERFACE)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
  target_sources(
    nlohmann_json
    INTERFACE
    FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/lib
    FILES
      ${PROJECT_SOURCE_DIR}/lib/nlohmann/json.hpp
  )
endif()

if(ENABLE_UI_QT)
  message(STATUS "Using vendored BASS(MIDI) library")
  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/bass ${CMAKE_CURRENT_BINARY_DIR}/lib/bass EXCLUDE_FROM_ALL)
endif()

if(USE_SYSTEM_FMT)
  find_package(fmt 12.1.1 REQUIRED)
else()  
  message(STATUS "Using vendored fmt library")
  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/fmt ${CMAKE_CURRENT_BINARY_DIR}/lib/fmt)
endif()  

if(USE_SYSTEM_SPDLOG)
  set(SPDLOG_FMT_EXTERNAL ${USE_SYSTEM_FMT} CACHE BOOL "Use external fmt library" FORCE)
  find_package(spdlog 1.15.3 REQUIRED)
else()
  message(STATUS "Using vendored spdlog library")
  set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library" FORCE)
  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/spdlog ${CMAKE_CURRENT_BINARY_DIR}/lib/spdlog EXCLUDE_FROM_ALL)
endif()

if(USE_SYSTEM_ZLIB)
  find_package(ZLIB REQUIRED)
else()
  message(STATUS "Using vendored zlib library")
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "[zlib] Enable Zlib Examples as tests" FORCE)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "[zlib] Enable building zlib shared library" FORCE)
  set(ZLIB_BUILD_STATIC ON CACHE BOOL "[zlib] Enable building zlib static library" FORCE)
  set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "[zlib] Enable building libminizip contrib library" FORCE)
  set(ZLIB_INSTALL OFF CACHE BOOL "[zlib] Enable installation of zlib" FORCE)

  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/zlib ${CMAKE_CURRENT_BINARY_DIR}/lib/zlib EXCLUDE_FROM_ALL)
  add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
  set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_LINK_LIBRARIES zlibstatic)
endif()

if(USE_SYSTEM_MINIZIP)
  find_package(minizip REQUIRED)
else()
  # Add minizip either linked to system zlib or vendored zlib
  set(MINIZIP_DIR "${PROJECT_SOURCE_DIR}/lib/zlib/contrib/minizip")

  add_library(minizip STATIC
          "${MINIZIP_DIR}/ioapi.c"
          "${MINIZIP_DIR}/unzip.c"
  )
  if(WIN32)
    target_sources(minizip PRIVATE "${MINIZIP_DIR}/iowin32.c")
  endif()

  target_include_directories(minizip PUBLIC "${MINIZIP_DIR}")
  target_link_libraries(minizip PUBLIC ZLIB::ZLIB)
endif()

if(USE_SYSTEM_UNARR)
  find_package(unarr 1.1.0 REQUIRED)
else()
  message(STATUS "Using vendored unarr library")
  set(ENABLE_7Z OFF CACHE BOOL "[unarr] Build with 7z support" FORCE)
  set(USE_SYSTEM_BZ2 OFF CACHE BOOL "[unarr] Build with system bzip2 if possible" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "[unarr] Build libunarr as a shared library" FORCE)
  set(USE_SYSTEM_ZLIB ${USE_SYSTEM_ZLIB} CACHE BOOL "[unarr] Build with system zlib if possible" FORCE)

  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/unarr ${CMAKE_CURRENT_BINARY_DIR}/lib/unarr EXCLUDE_FROM_ALL)
endif()

if(USE_SYSTEM_LIBCHDR)
  find_package(libchdr REQUIRED)
else()
  message(STATUS "Using vendored libchdr library")
  set(WITH_SYSTEM_ZLIB ${USE_SYSTEM_ZLIB} CACHE BOOL "[libchdr] Use system provided zlib library" FORCE)
  add_subdirectory(${PROJECT_SOURCE_DIR}/lib/libchdr ${CMAKE_CURRENT_BINARY_DIR}/lib/libchdr EXCLUDE_FROM_ALL)
endif()

# ~~~
# Build
# ~~~

add_library(vgmtranscore OBJECT)
target_sources(vgmtranscore
  PRIVATE
    LogManager.cpp
    Options.cpp
    Root.cpp
    components/FileLoader.cpp
    components/PSFFile.cpp
    components/SPCFile.cpp
    components/Scanner.cpp
    components/VGMColl.cpp
    components/VGMFile.cpp
    components/VGMItem.cpp
    components/VGMMiscFile.cpp
    components/VGMMultiSectionSeq.cpp
    components/VGMSampColl.cpp
    components/VGMTag.cpp
    components/instr/VGMInstrSet.cpp
    components/instr/VGMRgn.cpp
    components/instr/VGMSamp.cpp
    components/matcher/FilegroupMatcher.cpp
    components/matcher/Matcher.cpp
    components/seq/SeqEvent.cpp
    components/seq/SeqEventTimeIndex.cpp
    components/seq/SeqSlider.cpp
    components/seq/SeqTrack.cpp
    components/seq/TransposeTimeline.cpp
    components/seq/VGMSeq.cpp
    components/seq/VGMSeqNoTrks.cpp
    components/seq/VGMSeqSection.cpp
    conversion/DLSConversion.cpp
    conversion/DLSFile.cpp
    conversion/MidiFile.cpp
    conversion/RiffFile.cpp
    conversion/SF2Conversion.cpp
    conversion/SF2File.cpp
    conversion/SynthFile.cpp
    conversion/VGMExport.cpp
    formats/Akao/AkaoFormat.cpp
    formats/Akao/AkaoInstr.cpp
    formats/Akao/AkaoMatcher.cpp
    formats/Akao/AkaoScanner.cpp
    formats/Akao/AkaoSeq.cpp
    formats/AkaoSnes/AkaoSnesInstr.cpp
    formats/AkaoSnes/AkaoSnesScanner.cpp
    formats/AkaoSnes/AkaoSnesSeq.cpp
    formats/AsciiShuichiSnes/AsciiShuichiSnesInstr.cpp
    formats/AsciiShuichiSnes/AsciiShuichiSnesScanner.cpp
    formats/AsciiShuichiSnes/AsciiShuichiSnesSeq.cpp
    formats/CPS/CPS1Instr.cpp
    formats/CPS/CPS1Scanner.cpp
    formats/CPS/CPS1Seq.cpp
    formats/CPS/CPS1TrackV1.cpp
    formats/CPS/CPS1TrackV2.cpp
    formats/CPS/CPS2Instr.cpp
    formats/CPS/CPS2Scanner.cpp
    formats/CPS/CPS2Seq.cpp
    formats/CPS/CPS2TrackV1.cpp
    formats/CPS/CPS2TrackV2.cpp
    formats/CapcomSnes/CapcomSnesInstr.cpp
    formats/CapcomSnes/CapcomSnesScanner.cpp
    formats/CapcomSnes/CapcomSnesSeq.cpp
    formats/ChunSnes/ChunSnesInstr.cpp
    formats/ChunSnes/ChunSnesScanner.cpp
    formats/ChunSnes/ChunSnesSeq.cpp
    formats/CompileSnes/CompileSnesInstr.cpp
    formats/CompileSnes/CompileSnesScanner.cpp
    formats/CompileSnes/CompileSnesSeq.cpp
    formats/FFT/FFTInstr.cpp
    formats/FFT/FFTScanner.cpp
    formats/FFT/FFTSeq.cpp
    formats/FalcomSnes/FalcomSnesInstr.cpp
    formats/FalcomSnes/FalcomSnesScanner.cpp
    formats/FalcomSnes/FalcomSnesSeq.cpp
    formats/Format.cpp
    formats/GraphResSnes/GraphResSnesInstr.cpp
    formats/GraphResSnes/GraphResSnesScanner.cpp
    formats/GraphResSnes/GraphResSnesSeq.cpp
    formats/HOSA/HOSAInstr.cpp
    formats/HOSA/HOSAScanner.cpp
    formats/HOSA/HOSASeq.cpp
    formats/HeartBeatPS1/HeartBeatPS1Scanner.cpp
    formats/HeartBeatPS1/HeartBeatPS1Seq.cpp
    formats/HeartBeatSnes/HeartBeatSnesInstr.cpp
    formats/HeartBeatSnes/HeartBeatSnesScanner.cpp
    formats/HeartBeatSnes/HeartBeatSnesSeq.cpp
    formats/HudsonSnes/HudsonSnesInstr.cpp
    formats/HudsonSnes/HudsonSnesScanner.cpp
    formats/HudsonSnes/HudsonSnesSeq.cpp
    formats/ItikitiSnes/ItikitiSnesInstr.cpp
    formats/ItikitiSnes/ItikitiSnesScanner.cpp
    formats/ItikitiSnes/ItikitiSnesSeq.cpp
    formats/KonamiArcade/KonamiArcadeInstr.cpp
    formats/KonamiArcade/KonamiArcadeScanner.cpp
    formats/KonamiArcade/KonamiArcadeSeq.cpp
    formats/KonamiPS1/KonamiPS1Scanner.cpp
    formats/KonamiPS1/KonamiPS1Seq.cpp
    formats/KonamiSnes/KonamiSnesInstr.cpp
    formats/KonamiSnes/KonamiSnesScanner.cpp
    formats/KonamiSnes/KonamiSnesSeq.cpp
    formats/KonamiTMNT2/KonamiTMNT2Instr.cpp
    formats/KonamiTMNT2/KonamiTMNT2OPMInstr.cpp
    formats/KonamiTMNT2/KonamiTMNT2Scanner.cpp
    formats/KonamiTMNT2/KonamiTMNT2Seq.cpp
    formats/KonamiTMNT2/KonamiVendettaInstr.cpp
    formats/MP2k/MP2kInstrSet.cpp
    formats/MP2k/MP2kScanner.cpp
    formats/MP2k/MP2kSeq.cpp
    formats/MoriSnes/MoriSnesInstr.cpp
    formats/MoriSnes/MoriSnesScanner.cpp
    formats/MoriSnes/MoriSnesSeq.cpp
    formats/NDS/NDSInstrSet.cpp
    formats/NDS/NDSScanner.cpp
    formats/NDS/NDSSeq.cpp
    formats/NamcoSnes/NamcoSnesInstr.cpp
    formats/NamcoSnes/NamcoSnesScanner.cpp
    formats/NamcoSnes/NamcoSnesSeq.cpp
    formats/NeverlandSnes/NeverlandSnesScanner.cpp
    formats/NeverlandSnes/NeverlandSnesSeq.cpp
    formats/NinSnes/NinSnesInstr.cpp
    formats/NinSnes/NinSnesScanner.cpp
    formats/NinSnes/NinSnesSeq.cpp
    formats/Org/OrgScanner.cpp
    formats/Org/OrgSeq.cpp
    formats/PS1/PS1Seq.cpp
    formats/PS1/PS1SeqScanner.cpp
    formats/PS1/Vab.cpp
    formats/PandoraBoxSnes/PandoraBoxSnesInstr.cpp
    formats/PandoraBoxSnes/PandoraBoxSnesScanner.cpp
    formats/PandoraBoxSnes/PandoraBoxSnesSeq.cpp
    formats/PrismSnes/PrismSnesInstr.cpp
    formats/PrismSnes/PrismSnesScanner.cpp
    formats/PrismSnes/PrismSnesSeq.cpp
    formats/RareSnes/RareSnesInstr.cpp
    formats/RareSnes/RareSnesScanner.cpp
    formats/RareSnes/RareSnesSeq.cpp
    formats/SegSat/SegSatInstrSet.cpp
    formats/SegSat/SegSatScanner.cpp
    formats/SegSat/SegSatSeq.cpp
    formats/SoftCreatSnes/SoftCreatSnesScanner.cpp
    formats/SoftCreatSnes/SoftCreatSnesSeq.cpp
    formats/SonyPS2/SonyPS2InstrSet.cpp
    formats/SonyPS2/SonyPS2Scanner.cpp
    formats/SonyPS2/SonyPS2Seq.cpp
    formats/SquarePS2/SquarePS2Scanner.cpp
    formats/SquarePS2/SquarePS2Seq.cpp
    formats/SquarePS2/WD.cpp
    formats/SuzukiSnes/SuzukiSnesInstr.cpp
    formats/SuzukiSnes/SuzukiSnesScanner.cpp
    formats/SuzukiSnes/SuzukiSnesSeq.cpp
    formats/TamSoftPS1/TamSoftPS1Instr.cpp
    formats/TamSoftPS1/TamSoftPS1Scanner.cpp
    formats/TamSoftPS1/TamSoftPS1Seq.cpp
    formats/TriAcePS1/TriAcePS1InstrSet.cpp
    formats/TriAcePS1/TriAcePS1Scanner.cpp
    formats/TriAcePS1/TriAcePS1Seq.cpp
    formats/common/KonamiAdpcm.cpp
    formats/common/OkiAdpcm.cpp
    formats/common/PSXSPU.cpp
    formats/common/SNESDSP.cpp
    formats/common/YM2151InstrSet.cpp
    io/RawFile.cpp
    loaders/CHDLoader.cpp
    loaders/CPS3Decrypt.cpp
    loaders/KabukiDecrypt.cpp
    loaders/MAMELoader.cpp
    loaders/PSF2Loader.cpp
    loaders/PSFLoader.cpp
    loaders/RSNLoader.cpp
    loaders/SPC2Loader.cpp
    loaders/SPCLoader.cpp
    util/BytePattern.cpp
    util/ScaleConversion.cpp
    util/common.cpp
  PUBLIC
    FILE_SET HEADERS
    FILES
      LogItem.h
      LogManager.h
      Loop.h
      Options.h
      Root.h
    FILE_SET headers_formats TYPE HEADERS BASE_DIRS formats
    FILES
      formats/Akao/AkaoFormat.h
      formats/Akao/AkaoFormatVersion.h
      formats/Akao/AkaoInstr.h
      formats/Akao/AkaoMatcher.h
      formats/Akao/AkaoScanner.h
      formats/Akao/AkaoSeq.h
      formats/AkaoSnes/AkaoSnesFormat.h
      formats/AkaoSnes/AkaoSnesInstr.h
      formats/AkaoSnes/AkaoSnesScanner.h
      formats/AkaoSnes/AkaoSnesSeq.h
      formats/AsciiShuichiSnes/AsciiShuichiSnesFormat.h
      formats/AsciiShuichiSnes/AsciiShuichiSnesInstr.h
      formats/AsciiShuichiSnes/AsciiShuichiSnesScanner.h
      formats/AsciiShuichiSnes/AsciiShuichiSnesSeq.h
      formats/CPS/CPS1Instr.h
      formats/CPS/CPS1Scanner.h
      formats/CPS/CPS1Seq.h
      formats/CPS/CPS1TrackV1.h
      formats/CPS/CPS1TrackV2.h
      formats/CPS/CPS2Format.h
      formats/CPS/CPS2Instr.h
      formats/CPS/CPS2Scanner.h
      formats/CPS/CPS2Seq.h
      formats/CPS/CPS2TrackV1.h
      formats/CPS/CPS2TrackV2.h
      formats/CPS/CPSCommon.h
      formats/CapcomSnes/CapcomSnesFormat.h
      formats/CapcomSnes/CapcomSnesInstr.h
      formats/CapcomSnes/CapcomSnesScanner.h
      formats/CapcomSnes/CapcomSnesSeq.h
      formats/ChunSnes/ChunSnesFormat.h
      formats/ChunSnes/ChunSnesInstr.h
      formats/ChunSnes/ChunSnesScanner.h
      formats/ChunSnes/ChunSnesSeq.h
      formats/CompileSnes/CompileSnesFormat.h
      formats/CompileSnes/CompileSnesInstr.h
      formats/CompileSnes/CompileSnesScanner.h
      formats/CompileSnes/CompileSnesSeq.h
      formats/FFT/FFTFormat.h
      formats/FFT/FFTInstr.h
      formats/FFT/FFTScanner.h
      formats/FFT/FFTSeq.h
      formats/FalcomSnes/FalcomSnesFormat.h
      formats/FalcomSnes/FalcomSnesInstr.h
      formats/FalcomSnes/FalcomSnesScanner.h
      formats/FalcomSnes/FalcomSnesSeq.h
      formats/Format.h
      formats/GraphResSnes/GraphResSnesFormat.h
      formats/GraphResSnes/GraphResSnesInstr.h
      formats/GraphResSnes/GraphResSnesScanner.h
      formats/GraphResSnes/GraphResSnesSeq.h
      formats/HOSA/HOSAFormat.h
      formats/HOSA/HOSAInstr.h
      formats/HOSA/HOSAScanner.h
      formats/HOSA/HOSASeq.h
      formats/HeartBeatPS1/HeartBeatPS1Format.h
      formats/HeartBeatPS1/HeartBeatPS1Scanner.h
      formats/HeartBeatPS1/HeartBeatPS1Seq.h
      formats/HeartBeatSnes/HeartBeatSnesFormat.h
      formats/HeartBeatSnes/HeartBeatSnesInstr.h
      formats/HeartBeatSnes/HeartBeatSnesScanner.h
      formats/HeartBeatSnes/HeartBeatSnesSeq.h
      formats/HudsonSnes/HudsonSnesFormat.h
      formats/HudsonSnes/HudsonSnesInstr.h
      formats/HudsonSnes/HudsonSnesScanner.h
      formats/HudsonSnes/HudsonSnesSeq.h
      formats/ItikitiSnes/ItikitiSnesFormat.h
      formats/ItikitiSnes/ItikitiSnesInstr.h
      formats/ItikitiSnes/ItikitiSnesScanner.h
      formats/ItikitiSnes/ItikitiSnesSeq.h
      formats/KonamiArcade/KonamiArcadeDefinitions.h
      formats/KonamiArcade/KonamiArcadeFormat.h
      formats/KonamiArcade/KonamiArcadeInstr.h
      formats/KonamiArcade/KonamiArcadeScanner.h
      formats/KonamiArcade/KonamiArcadeSeq.h
      formats/KonamiPS1/KonamiPS1Format.h
      formats/KonamiPS1/KonamiPS1Scanner.h
      formats/KonamiPS1/KonamiPS1Seq.h
      formats/KonamiSnes/KonamiSnesFormat.h
      formats/KonamiSnes/KonamiSnesInstr.h
      formats/KonamiSnes/KonamiSnesScanner.h
      formats/KonamiSnes/KonamiSnesSeq.h
      formats/KonamiTMNT2/KonamiTMNT2Definitions.h
      formats/KonamiTMNT2/KonamiTMNT2Format.h
      formats/KonamiTMNT2/KonamiTMNT2Instr.h
      formats/KonamiTMNT2/KonamiTMNT2OPMInstr.h
      formats/KonamiTMNT2/KonamiTMNT2Scanner.h
      formats/KonamiTMNT2/KonamiTMNT2Seq.h
      formats/KonamiTMNT2/KonamiVendettaInstr.h
      formats/MP2k/MP2kFormat.h
      formats/MP2k/MP2kInstrSet.h
      formats/MP2k/MP2kScanner.h
      formats/MP2k/MP2kSeq.h
      formats/MoriSnes/MoriSnesFormat.h
      formats/MoriSnes/MoriSnesInstr.h
      formats/MoriSnes/MoriSnesScanner.h
      formats/MoriSnes/MoriSnesSeq.h
      formats/NDS/NDSFormat.h
      formats/NDS/NDSInstrSet.h
      formats/NDS/NDSScanner.h
      formats/NDS/NDSSeq.h
      formats/NamcoSnes/NamcoSnesFormat.h
      formats/NamcoSnes/NamcoSnesInstr.h
      formats/NamcoSnes/NamcoSnesScanner.h
      formats/NamcoSnes/NamcoSnesSeq.h
      formats/NeverlandSnes/NeverlandSnesFormat.h
      formats/NeverlandSnes/NeverlandSnesScanner.h
      formats/NeverlandSnes/NeverlandSnesSeq.h
      formats/NinSnes/NinSnesFormat.h
      formats/NinSnes/NinSnesInstr.h
      formats/NinSnes/NinSnesScanner.h
      formats/NinSnes/NinSnesSeq.h
      formats/Org/OrgFormat.h
      formats/Org/OrgScanner.h
      formats/Org/OrgSeq.h
      formats/PS1/PS1Format.h
      formats/PS1/PS1Seq.h
      formats/PS1/PS1SeqScanner.h
      formats/PS1/Vab.h
      formats/PandoraBoxSnes/PandoraBoxSnesFormat.h
      formats/PandoraBoxSnes/PandoraBoxSnesInstr.h
      formats/PandoraBoxSnes/PandoraBoxSnesScanner.h
      formats/PandoraBoxSnes/PandoraBoxSnesSeq.h
      formats/PrismSnes/PrismSnesFormat.h
      formats/PrismSnes/PrismSnesInstr.h
      formats/PrismSnes/PrismSnesScanner.h
      formats/PrismSnes/PrismSnesSeq.h
      formats/RareSnes/RareSnesFormat.h
      formats/RareSnes/RareSnesInstr.h
      formats/RareSnes/RareSnesScanner.h
      formats/RareSnes/RareSnesSeq.h
      formats/SegSat/SegSatFormat.h
      formats/SegSat/SegSatInstrSet.h
      formats/SegSat/SegSatScanner.h
      formats/SegSat/SegSatSeq.h
      formats/SoftCreatSnes/SoftCreatSnesFormat.h
      formats/SoftCreatSnes/SoftCreatSnesScanner.h
      formats/SoftCreatSnes/SoftCreatSnesSeq.h
      formats/SonyPS2/SonyPS2Format.h
      formats/SonyPS2/SonyPS2InstrSet.h
      formats/SonyPS2/SonyPS2Scanner.h
      formats/SonyPS2/SonyPS2Seq.h
      formats/SquarePS2/SquarePS2Format.h
      formats/SquarePS2/SquarePS2Scanner.h
      formats/SquarePS2/SquarePS2Seq.h
      formats/SquarePS2/WD.h
      formats/SuzukiSnes/SuzukiSnesFormat.h
      formats/SuzukiSnes/SuzukiSnesInstr.h
      formats/SuzukiSnes/SuzukiSnesScanner.h
      formats/SuzukiSnes/SuzukiSnesSeq.h
      formats/TamSoftPS1/TamSoftPS1Format.h
      formats/TamSoftPS1/TamSoftPS1Instr.h
      formats/TamSoftPS1/TamSoftPS1Scanner.h
      formats/TamSoftPS1/TamSoftPS1Seq.h
      formats/TriAcePS1/TriAcePS1Format.h
      formats/TriAcePS1/TriAcePS1InstrSet.h
      formats/TriAcePS1/TriAcePS1Scanner.h
      formats/TriAcePS1/TriAcePS1Seq.h
    FILE_SET headers_formats_common TYPE HEADERS BASE_DIRS formats/common
    FILES
      formats/common/KonamiAdpcm.h
      formats/common/OkiAdpcm.h
      formats/common/PSXSPU.h
      formats/common/SNESDSP.h
      formats/common/YM2151.h
      formats/common/YM2151InstrSet.h
    FILE_SET headers_util TYPE HEADERS BASE_DIRS util
    FILES
      util/BytePattern.h
      util/MidiConstants.h
      util/ScaleConversion.h
      util/common.h
      util/constevalHelpers.h
      util/decompression.h
      util/helper.h
    FILE_SET headers_io TYPE HEADERS BASE_DIRS io
    FILES
      io/RawFile.h
    FILE_SET headers_components TYPE HEADERS BASE_DIRS components
    FILES
      components/FileLoader.h
      components/PSFFile.h
      components/SPCFile.h
      components/Scanner.h
      components/ScannerManager.h
      components/SynthType.h
      components/VGMColl.h
      components/VGMFile.h
      components/VGMItem.h
      components/VGMMiscFile.h
      components/VGMMultiSectionSeq.h
      components/VGMSampColl.h
      components/VGMTag.h
    FILE_SET headers_components_instr TYPE HEADERS BASE_DIRS components/instr
    FILES
      components/instr/VGMInstrSet.h
      components/instr/VGMRgn.h
      components/instr/VGMSamp.h
    FILE_SET headers_components_matcher TYPE HEADERS BASE_DIRS components/matcher
    FILES
      components/matcher/FilegroupMatcher.h
      components/matcher/FilenameMatcher.h
      components/matcher/GetIdMatcher.h
      components/matcher/Matcher.h
      components/matcher/SimpleMatcher.h
    FILE_SET headers_components_seq TYPE HEADERS BASE_DIRS components/seq
    FILES
      components/seq/SeqEvent.h
      components/seq/SeqEventTimeIndex.h
      components/seq/SeqSlider.h
      components/seq/SeqTrack.h
      components/seq/TransposeTimeline.h
      components/seq/VGMSeq.h
      components/seq/VGMSeqNoTrks.h
      components/seq/VGMSeqSection.h
    FILE_SET headers_conversion TYPE HEADERS BASE_DIRS conversion
    FILES
      conversion/DLSConversion.h
      conversion/DLSFile.h
      conversion/MidiFile.h
      conversion/RiffFile.h
      conversion/SF2Conversion.h
      conversion/SF2File.h
      conversion/SynthFile.h
      conversion/VGMExport.h
    FILE_SET headers_loaders TYPE HEADERS BASE_DIRS loaders
    FILES
      loaders/CHDLoader.h
      loaders/CPS3Decrypt.h
      loaders/KabukiDecrypt.h
      loaders/LoaderManager.h
      loaders/MAMELoader.h
      loaders/PSF2Loader.h
      loaders/PSFLoader.h
      loaders/RSNLoader.h
      loaders/SPC2Loader.h
      loaders/SPCLoader.h
)

configure_file("version.h.in" "version.h")
target_include_directories(vgmtranscore PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
  vgmtranscore
  PUBLIC spdlog::spdlog unarr ZLIB::ZLIB minizip mio chdr-static nlohmann_json::nlohmann_json
)
target_compile_features(vgmtranscore PUBLIC cxx_std_20)

configure_file(${PROJECT_SOURCE_DIR}/bin/mame_roms.json mame_roms.json COPYONLY)
target_compile_definitions(vgmtranscore PUBLIC "DEV_ENV_BUILD_TREE=\"${CMAKE_CURRENT_BINARY_DIR}\"")

if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|Linux")
  if(NOT VGMTRANS_DATA_DIR)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_DATADIR}")
      set(VGMTRANS_DATA_DIR "${CMAKE_INSTALL_DATADIR}/vgmtrans")
    else()
      set(VGMTRANS_DATA_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/vgmtrans")
    endif()
  endif()
  target_compile_definitions(vgmtranscore PUBLIC "VGMTRANS_DATA_DIR=\"${VGMTRANS_DATA_DIR}\"")
endif()

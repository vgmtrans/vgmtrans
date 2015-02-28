cmake_minimum_required(VERSION 2.8.4)
project(vgmtrans)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)
# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Widgets finds its own dependencies (QtGui and QtCore).
find_package(Qt5Widgets REQUIRED)
# The Qt5Widgets_INCLUDES also includes the include directories for
# dependencies QtCore and QtGui
include_directories(${Qt5Widgets_INCLUDES})
# We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
add_definitions(${Qt5Widgets_DEFINITIONS})
# Executables fail to build with Qt 5 in the default configuration
# without -fPIE. We add that here.
set(CMAKE_CXX_FLAGS "${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")


#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set ( CMAKE_OSX_ARCHITECTURES "x86_64;" )
#set ( CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libstdc++ -g3 -Wall -O0" )
set ( CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libc++ -g3 -Wall -O0" )
#find_package(Qt5 COMPONENTS Widgets Declarative)


include_directories(
    lib/zlib/include
    lib/minizip
    lib/tinyxml
    lib/fluidsynth/include
    lib/fluidsynth/mem_sfloader
    main
    main/formats
    main/loaders
    ui/qt
)

set(SOURCE_FILES

    ui/qt/main.cpp
    ui/qt/mainwindow.cpp
    ui/qt/droparea.cpp
    ui/qt/dropsitewindow.cpp
    ui/qt/osdepend.cpp

    main/formats/AkaoFormat.cpp
    main/formats/AkaoFormat.h
    main/formats/AkaoInstr.cpp
    main/formats/AkaoInstr.h
    main/formats/AkaoScanner.cpp
    main/formats/AkaoScanner.h
    main/formats/AkaoSeq.cpp
    main/formats/AkaoSeq.h
    main/formats/AkaoSnesFormat.h
    main/formats/AkaoSnesInstr.cpp
    main/formats/AkaoSnesInstr.h
    main/formats/AkaoSnesScanner.cpp
    main/formats/AkaoSnesScanner.h
    main/formats/AkaoSnesSeq.cpp
    main/formats/AkaoSnesSeq.h
    main/formats/CapcomSnesFormat.h
    main/formats/CapcomSnesInstr.cpp
    main/formats/CapcomSnesInstr.h
    main/formats/CapcomSnesScanner.cpp
    main/formats/CapcomSnesScanner.h
    main/formats/CapcomSnesSeq.cpp
    main/formats/CapcomSnesSeq.h
    main/formats/ChunSnesFormat.h
    main/formats/ChunSnesInstr.cpp
    main/formats/ChunSnesInstr.h
    main/formats/ChunSnesScanner.cpp
    main/formats/ChunSnesScanner.h
    main/formats/ChunSnesSeq.cpp
    main/formats/ChunSnesSeq.h
    main/formats/CompileSnesFormat.h
    main/formats/CompileSnesInstr.cpp
    main/formats/CompileSnesInstr.h
    main/formats/CompileSnesScanner.cpp
    main/formats/CompileSnesScanner.h
    main/formats/CompileSnesSeq.cpp
    main/formats/CompileSnesSeq.h
    main/formats/FFTFormat.h
    main/formats/FFTInstr.cpp
    main/formats/FFTInstr.h
    main/formats/FFTScanner.cpp
    main/formats/FFTScanner.h
    main/formats/FFTSeq.cpp
    main/formats/FFTSeq.h
    main/formats/GraphResSnesFormat.h
    main/formats/GraphResSnesInstr.cpp
    main/formats/GraphResSnesInstr.h
    main/formats/GraphResSnesScanner.cpp
    main/formats/GraphResSnesScanner.h
    main/formats/GraphResSnesSeq.cpp
    main/formats/GraphResSnesSeq.h
    main/formats/HeartBeatPS1Format.h
    main/formats/HeartBeatPS1Seq.cpp
    main/formats/HeartBeatPS1Seq.h
    main/formats/HeartBeatPS1SeqScanner.cpp
    main/formats/HeartBeatPS1SeqScanner.h
    main/formats/HeartBeatSnesFormat.h
    main/formats/HeartBeatSnesInstr.cpp
    main/formats/HeartBeatSnesInstr.h
    main/formats/HeartBeatSnesScanner.cpp
    main/formats/HeartBeatSnesScanner.h
    main/formats/HeartBeatSnesSeq.cpp
    main/formats/HeartBeatSnesSeq.h
    main/formats/HOSAFormat.h
    main/formats/HOSAInstr.cpp
    main/formats/HOSAInstr.h
    main/formats/HOSAScanner.cpp
    main/formats/HOSAScanner.h
    main/formats/HOSASeq.cpp
    main/formats/HOSASeq.h
    main/formats/HudsonSnesFormat.h
    main/formats/HudsonSnesInstr.cpp
    main/formats/HudsonSnesInstr.h
    main/formats/HudsonSnesScanner.cpp
    main/formats/HudsonSnesScanner.h
    main/formats/HudsonSnesSeq.cpp
    main/formats/HudsonSnesSeq.h
    main/formats/KonamiGXFormat.h
    main/formats/KonamiGXScanner.cpp
    main/formats/KonamiGXScanner.h
    main/formats/KonamiGXSeq.cpp
    main/formats/KonamiGXSeq.h
    main/formats/KonamiSnesFormat.h
    main/formats/KonamiSnesInstr.cpp
    main/formats/KonamiSnesInstr.h
    main/formats/KonamiSnesScanner.cpp
    main/formats/KonamiSnesScanner.h
    main/formats/KonamiSnesSeq.cpp
    main/formats/KonamiSnesSeq.h
    main/formats/MoriSnesFormat.h
    main/formats/MoriSnesInstr.cpp
    main/formats/MoriSnesInstr.h
    main/formats/MoriSnesScanner.cpp
    main/formats/MoriSnesScanner.h
    main/formats/MoriSnesSeq.cpp
    main/formats/MoriSnesSeq.h
    main/formats/MP2kFormat.h
    main/formats/MP2kScanner.cpp
    main/formats/MP2kScanner.h
    main/formats/MP2kSeq.cpp
    main/formats/MP2kSeq.h
    main/formats/NamcoSnesFormat.h
    main/formats/NamcoSnesScanner.cpp
    main/formats/NamcoSnesScanner.h
    main/formats/NamcoSnesSeq.cpp
    main/formats/NamcoSnesSeq.h
    main/formats/NDSFormat.h
    main/formats/NDSInstrSet.cpp
    main/formats/NDSInstrSet.h
    main/formats/NDSScanner.cpp
    main/formats/NDSScanner.h
    main/formats/NDSSeq.cpp
    main/formats/NDSSeq.h
    main/formats/OrgFormat.h
    main/formats/OrgScanner.cpp
    main/formats/OrgScanner.h
    main/formats/OrgSeq.cpp
    main/formats/OrgSeq.h
    main/formats/PandoraBoxSnesFormat.h
    main/formats/PandoraBoxSnesInstr.cpp
    main/formats/PandoraBoxSnesInstr.h
    main/formats/PandoraBoxSnesScanner.cpp
    main/formats/PandoraBoxSnesScanner.h
    main/formats/PandoraBoxSnesSeq.cpp
    main/formats/PandoraBoxSnesSeq.h
    main/formats/PS1Format.h
    main/formats/PS1Seq.cpp
    main/formats/PS1Seq.h
    main/formats/PS1SeqScanner.cpp
    main/formats/PS1SeqScanner.h
    main/formats/PSXSPU.cpp
    main/formats/PSXSPU.h
    main/formats/QSoundFormat.h
    main/formats/QSoundInstr.cpp
    main/formats/QSoundInstr.h
    main/formats/QSoundScanner.cpp
    main/formats/QSoundScanner.h
    main/formats/QSoundSeq.cpp
    main/formats/QSoundSeq.h
    main/formats/RareSnesFormat.h
    main/formats/RareSnesInstr.cpp
    main/formats/RareSnesInstr.h
    main/formats/RareSnesScanner.cpp
    main/formats/RareSnesScanner.h
    main/formats/RareSnesSeq.cpp
    main/formats/RareSnesSeq.h
    main/formats/SegSatFormat.h
    main/formats/SegSatScanner.cpp
    main/formats/SegSatScanner.h
    main/formats/SegSatSeq.cpp
    main/formats/SegSatSeq.h
    main/formats/SNESDSP.cpp
    main/formats/SNESDSP.h
    main/formats/SoftCreatSnesFormat.h
    main/formats/SoftCreatSnesScanner.cpp
    main/formats/SoftCreatSnesScanner.h
    main/formats/SoftCreatSnesSeq.cpp
    main/formats/SoftCreatSnesSeq.h
    main/formats/SonyPS2Format.h
    main/formats/SonyPS2InstrSet.cpp
    main/formats/SonyPS2InstrSet.h
    main/formats/SonyPS2Scanner.cpp
    main/formats/SonyPS2Scanner.h
    main/formats/SonyPS2Seq.cpp
    main/formats/SonyPS2Seq.h
    main/formats/SquarePS2Format.h
    main/formats/SquarePS2Scanner.cpp
    main/formats/SquarePS2Scanner.h
    main/formats/SquarePS2Seq.cpp
    main/formats/SquarePS2Seq.h
    main/formats/SuzukiSnesFormat.h
    main/formats/SuzukiSnesInstr.cpp
    main/formats/SuzukiSnesInstr.h
    main/formats/SuzukiSnesScanner.cpp
    main/formats/SuzukiSnesScanner.h
    main/formats/SuzukiSnesSeq.cpp
    main/formats/SuzukiSnesSeq.h
    main/formats/TriAcePS1Format.h
    main/formats/TriAcePS1InstrSet.cpp
    main/formats/TriAcePS1InstrSet.h
    main/formats/TriAcePS1Scanner.cpp
    main/formats/TriAcePS1Scanner.h
    main/formats/TriAcePS1Seq.cpp
    main/formats/TriAcePS1Seq.h
    main/formats/Vab.cpp
    main/formats/Vab.h
    main/formats/WD.cpp
    main/formats/WD.h

    main/loaders/GSFLoader.cpp
    main/loaders/GSFLoader.h
    main/loaders/KabukiDecrypt.cpp
    main/loaders/KabukiDecrypt.h
    main/loaders/MAMELoader.cpp
    main/loaders/MAMELoader.h
    main/loaders/NCSFLoader.cpp
    main/loaders/NCSFLoader.h
    main/loaders/NDS2SFLoader.cpp
    main/loaders/NDS2SFLoader.h
    main/loaders/PSF1Loader.cpp
    main/loaders/PSF1Loader.h
    main/loaders/PSF2Loader.cpp
    main/loaders/PSF2Loader.h
    main/loaders/SNSFLoader.cpp
    main/loaders/SNSFLoader.h
    main/loaders/SPCLoader.cpp
    main/loaders/SPCLoader.h

    main/BytePattern.cpp
    main/BytePattern.h
    main/common.cpp
    main/common.h
    main/DataSeg.cpp
    main/DataSeg.h
    main/datetime.cpp
    main/datetime.h
    main/DLSFile.cpp
    main/DLSFile.h
    main/ExtensionDiscriminator.cpp
    main/ExtensionDiscriminator.h
    main/Format.cpp
    main/Format.h
    main/helper.h
    main/Loader.cpp
    main/Loader.h
    main/LogItem.cpp
    main/LogItem.h
    main/Loop.h
    main/Matcher.cpp
    main/Matcher.h
    main/Menu.cpp
    main/Menu.h
    main/MidiFile.cpp
    main/MidiFile.h
    main/Options.cpp
    main/Options.h
    main/pch.h
    main/PSFFile.cpp
    main/PSFFile.h
    main/RawFile.cpp
    main/RawFile.h
    main/RiffFile.cpp
    main/RiffFile.h
    main/Root.cpp
    main/Root.h
    main/ScaleConversion.cpp
    main/ScaleConversion.h
    main/Scanner.cpp
    main/Scanner.h
    main/SeqEvent.cpp
    main/SeqEvent.h
    main/SeqNote.h
    main/SeqTrack.cpp
    main/SeqTrack.h
    main/SF2File.cpp
    main/SF2File.h
    main/SynthFile.cpp
    main/SynthFile.h
    main/VGMColl.cpp
    main/VGMColl.h
    main/VGMFile.cpp
    main/VGMFile.h
    main/VGMInstrSet.cpp
    main/VGMInstrSet.h
    main/VGMItem.cpp
    main/VGMItem.h
    main/VGMMiscFile.cpp
    main/VGMMiscFile.h
    main/VGMMultiSectionSeq.cpp
    main/VGMMultiSectionSeq.h
    main/VGMRgn.cpp
    main/VGMRgn.h
    main/VGMSamp.cpp
    main/VGMSamp.h
    main/VGMSampColl.cpp
    main/VGMSampColl.h
    main/VGMSeq.cpp
    main/VGMSeq.h
    main/VGMSeqNoTrks.cpp
    main/VGMSeqNoTrks.h
    main/VGMSeqSection.cpp
    main/VGMSeqSection.h
    main/VGMTag.cpp
    main/VGMTag.h

    lib/tinyxml/src/tinystr.cpp
    lib/tinyxml/src/tinyxml.cpp
    lib/tinyxml/src/tinyxmlerror.cpp
    lib/tinyxml/src/tinyxmlparser.cpp

    lib/minizip/ioapi.c
    lib/minizip/mztools.c
    lib/minizip/unzip.c
    lib/minizip/zip.c

    ui/qt/QtVGMRoot.cpp
    ui/qt/VGMFileListView.cpp
    ui/qt/RawFileListView.cpp
    ui/qt/VGMCollListView.cpp
    ui/qt/MusicPlayer.cpp

    lib/fluidsynth/mem_sfloader/mem_sfloader.c
    lib/fluidsynth/mem_sfloader/vgmtrans_fluid_midi.c

    lib/fluidsynth/include/fluidsynth/audio.h
    lib/fluidsynth/include/fluidsynth/event.h
    lib/fluidsynth/include/fluidsynth/gen.h
    lib/fluidsynth/include/fluidsynth/log.h
    lib/fluidsynth/include/fluidsynth/midi.h
    lib/fluidsynth/include/fluidsynth/misc.h
    lib/fluidsynth/include/fluidsynth/mod.h
    lib/fluidsynth/include/fluidsynth/ramsfont.h
    lib/fluidsynth/include/fluidsynth/seq.h
    lib/fluidsynth/include/fluidsynth/seqbind.h
    lib/fluidsynth/include/fluidsynth/settings.h
    lib/fluidsynth/include/fluidsynth/sfont.h
    lib/fluidsynth/include/fluidsynth/shell.h
    lib/fluidsynth/include/fluidsynth/synth.h
    lib/fluidsynth/include/fluidsynth/types.h
    lib/fluidsynth/include/fluidsynth/version.h
    lib/fluidsynth/include/fluidsynth/voice.h
    lib/fluidsynth/include/fluidsynth.h
    lib/fluidsynth/mem_sfloader/mem_sfloader.h
    lib/fluidsynth/mem_sfloader/vgmtrans_fluid_midi.h

    #lib/minizip/crypt.h
    #lib/minizip/ioapi.h
    #lib/minizip/mztools.h
    #lib/minizip/unzip.h
    #lib/minizip/zip.h

    #lib/tinyxml/src/tinystr.h
    #lib/tinyxml/src/tinyxml.h
    lib/tinyxml/tinystr.h
    lib/tinyxml/tinyxml.h

    lib/zlib/include/ioapi.h
    lib/zlib/include/unzip.h
    lib/zlib/include/zconf.h
    lib/zlib/include/zlib.h

    ui/qt/mainwindow.h
    ui/qt/droparea.h
    ui/qt/dropsitewindow.h
    ui/qt/osdepend.h
    ui/qt/QtVGMRoot.h
    ui/qt/VGMFileListView.h
    ui/qt/RawFileListView.h
    ui/qt/VGMCollListView.h
    ui/qt/MusicPlayer.h
)


qt5_add_resources(UI_RESOURCES ui/qt/resources/resources.qrc)

add_executable(vgmtrans ${SOURCE_FILES} ${UI_RESOURCES})

# The Qt5Widgets_LIBRARIES variable also includes QtGui and QtCore
target_link_libraries(vgmtrans ${Qt5Widgets_LIBRARIES})

find_package (ZLIB REQUIRED)
if (ZLIB_FOUND)
  include_directories(${ZLIB_INCLUDE_DIRS})
  target_link_libraries (vgmtrans ${ZLIB_LIBRARIES})
endif (ZLIB_FOUND)

find_package(GLIB REQUIRED)
include_directories(${GLIB_INCLUDE_DIRS})
#set(LIBS ${LIBS} ${GLIB_LIBRARIES})
target_link_libraries(vgmtrans ${GLIB_LIBRARIES})

find_package(READLINE REQUIRED)
include_directories(${READLINE_INCLUDE_DIRS})
#set(LIBS ${LIBS} ${GLIB_LIBRARIES})
target_link_libraries(vgmtrans ${READLINE_LIBRARIES})

find_package (FLUIDSYNTH REQUIRED)
include_directories(${FLUIDSYNTH_INCLUDE_DIRS})
#set(LIBS ${LIBS} ${GLIB_LIBRARIES})
target_link_libraries(vgmtrans ${FLUIDSYNTH_LIBRARIES})

IF (APPLE)
   FIND_LIBRARY(COREMIDI_LIBRARY CoreMIDI)
   FIND_LIBRARY(COREAUDIO_LIBRARY CoreAudio)
   FIND_LIBRARY(AUDIOUNIT_LIBRARY AudioUnit)
   FIND_LIBRARY(COREFOUNDATION_LIBRARY CoreFoundation)
   FIND_LIBRARY(CORESERVICES_LIBRARY CoreServices)
   TARGET_LINK_LIBRARIES(vgmtrans
    ${COREMIDI_LIBRARY}
    ${COREAUDIO_LIBRARY}
    ${AUDIOUNIT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${CORESERVICES_LIBRARY}
    )
ENDIF (APPLE)

add_definitions(-DTIXML_USE_STL)


#find_library (fluidsynth PATHS lib/fluidsynth/lib)
#link_directories(lib/fluidsynth/lib)

#target_link_libraries(vgmtrans ${CMAKE_SOURCE_DIR}/lib/fluidsynth/lib/libfluidsynth.a)
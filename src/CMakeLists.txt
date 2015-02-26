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
    lib/tinyxml/src
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
    main/formats/AkaoInstr.cpp
    main/formats/AkaoScanner.cpp
    main/formats/AkaoSeq.cpp
    main/formats/CapcomSnesInstr.cpp
    main/formats/CapcomSnesScanner.cpp
    main/formats/CapcomSnesSeq.cpp
    main/formats/FFT.cpp
    main/formats/FFTInstr.cpp
    main/formats/FFTScanner.cpp
    main/formats/HeartBeatPS1Seq.cpp
    main/formats/HeartBeatPS1SeqScanner.cpp
    main/formats/HOSA.cpp
    main/formats/HOSAInstr.cpp
    main/formats/HOSAScanner.cpp
    main/formats/KonamiGXScanner.cpp
    main/formats/KonamiGXSeq.cpp
    main/formats/KonamiSnesInstr.cpp
    main/formats/KonamiSnesScanner.cpp
    main/formats/KonamiSnesSeq.cpp
    main/formats/MP2k.cpp
    main/formats/MP2kScanner.cpp
    main/formats/NDSInstrSet.cpp
    main/formats/NDSScanner.cpp
    main/formats/NDSSeq.cpp
    main/formats/NinSnesInstr.cpp
    main/formats/NinSnesScanner.cpp
    main/formats/NinSnesSeq.cpp
    main/formats/OrgScanner.cpp
    main/formats/OrgSeq.cpp
    main/formats/PS1Seq.cpp
    main/formats/PS1SeqScanner.cpp
    main/formats/PSXSPU.cpp
    main/formats/QSoundInstr.cpp
    main/formats/QSoundScanner.cpp
    main/formats/QSoundSeq.cpp
    main/formats/RareSnesInstr.cpp
    main/formats/RareSnesScanner.cpp
    main/formats/RareSnesSeq.cpp
    main/formats/SegSat.cpp
    main/formats/SegSatScanner.cpp
    main/formats/SNESDSP.cpp
    main/formats/SonyPS2InstrSet.cpp
    main/formats/SonyPS2Scanner.cpp
    main/formats/SonyPS2Seq.cpp
    main/formats/SquarePS2Scanner.cpp
    main/formats/SquarePS2Seq.cpp
    main/formats/SquareSnesSeq.cpp
    main/formats/SquSnesScanner.cpp
    main/formats/TaitoF3Scanner.cpp
    main/formats/TriAcePS1InstrSet.cpp
    main/formats/TriAcePS1Scanner.cpp
    main/formats/TriAcePS1Seq.cpp
    main/formats/Vab.cpp
    main/formats/WD.cpp
    main/loaders/GSFLoader.cpp
    main/loaders/KabukiDecrypt.cpp
    main/loaders/MAMELoader.cpp
    main/loaders/NCSFLoader.cpp
    main/loaders/NDS2SFLoader.cpp
    main/loaders/PSF1Loader.cpp
    main/loaders/PSF2Loader.cpp
    main/loaders/SNSFLoader.cpp
    main/loaders/SPCLoader.cpp
    main/BytePattern.cpp
    main/common.cpp
    main/DataSeg.cpp
    main/datetime.cpp
    main/DLSFile.cpp
    main/ExtensionDiscriminator.cpp
    main/Format.cpp
    main/Loader.cpp
    main/LogItem.cpp
    main/Matcher.cpp
    main/Menu.cpp
    main/MidiFile.cpp
    main/Options.cpp
    main/PSFFile.cpp
    main/RawFile.cpp
    main/RiffFile.cpp
    main/Root.cpp
    main/ScaleConversion.cpp
    main/Scanner.cpp
    main/SeqEvent.cpp
    main/SeqTrack.cpp
    main/SeqVoiceAllocator.cpp
    main/SF2File.cpp
    main/SynthFile.cpp
    main/VGMColl.cpp
    main/VGMFile.cpp
    main/VGMInstrSet.cpp
    main/VGMItem.cpp
    main/VGMMiscFile.cpp
    main/VGMRgn.cpp
    main/VGMSamp.cpp
    main/VGMSampColl.cpp
    main/VGMSeq.cpp
    main/VGMSeqNoTrks.cpp
    main/VGMTag.cpp
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

    ui/qt/mainwindow.h
    ui/qt/droparea.h
    ui/qt/dropsitewindow.h
    main/formats/AkaoFormat.h
    main/formats/AkaoInstr.h
    main/formats/AkaoScanner.h
    main/formats/AkaoSeq.h
    main/formats/CapcomSnesFormat.h
    main/formats/CapcomSnesInstr.h
    main/formats/CapcomSnesScanner.h
    main/formats/CapcomSnesSeq.h
    main/formats/FFT.h
    main/formats/FFTFormat.h
    main/formats/FFTInstr.h
    main/formats/FFTScanner.h
    main/formats/HeartBeatPS1Format.h
    main/formats/HeartBeatPS1Seq.h
    main/formats/HeartBeatPS1SeqScanner.h
    main/formats/HOSA.h
    main/formats/HOSAFormat.h
    main/formats/HOSAInstr.h
    main/formats/HOSAScanner.h
    main/formats/KonamiGXFormat.h
    main/formats/KonamiGXScanner.h
    main/formats/KonamiGXSeq.h
    main/formats/KonamiSnesFormat.h
    main/formats/KonamiSnesInstr.h
    main/formats/KonamiSnesScanner.h
    main/formats/KonamiSnesSeq.h
    main/formats/MP2k.h
    main/formats/MP2kFormat.h
    main/formats/MP2kScanner.h
    main/formats/NDSFormat.h
    main/formats/NDSInstrSet.h
    main/formats/NDSScanner.h
    main/formats/NDSSeq.h
    main/formats/NinSnesFormat.h
    main/formats/NinSnesInstr.h
    main/formats/NinSnesScanner.h
    main/formats/NinSnesSeq.h
    main/formats/OrgFormat.h
    main/formats/OrgScanner.h
    main/formats/OrgSeq.h
    main/formats/PS1Format.h
    main/formats/PS1Seq.h
    main/formats/PS1SeqScanner.h
    main/formats/PSXSPU.h
    main/formats/QSoundFormat.h
    main/formats/QSoundInstr.h
    main/formats/QSoundScanner.h
    main/formats/QSoundSeq.h
    main/formats/RareSnesFormat.h
    main/formats/RareSnesInstr.h
    main/formats/RareSnesScanner.h
    main/formats/RareSnesSeq.h
    main/formats/SegSat.h
    main/formats/SegSatFormat.h
    main/formats/SegSatScanner.h
    main/formats/SNESDSP.h
    main/formats/SonyPS2Format.h
    main/formats/SonyPS2InstrSet.h
    main/formats/SonyPS2Scanner.h
    main/formats/SonyPS2Seq.h
    main/formats/SquarePS2Format.h
    main/formats/SquarePS2Scanner.h
    main/formats/SquarePS2Seq.h
    main/formats/SquSnesFormat.h
    main/formats/SquSnesScanner.h
    main/formats/SquSnesSeq.h
    main/formats/TaitoF3Format.h
    main/formats/TaitoF3Scanner.h
    main/formats/TriAcePS1Format.h
    main/formats/TriAcePS1InstrSet.h
    main/formats/TriAcePS1Scanner.h
    main/formats/TriAcePS1Seq.h
    main/formats/Vab.h
    main/formats/WD.h
    main/loaders/GSFLoader.h
    main/loaders/KabukiDecrypt.h
    main/loaders/MAMELoader.h
    main/loaders/NCSFLoader.h
    main/loaders/NDS2SFLoader.h
    main/loaders/PSF1Loader.h
    main/loaders/PSF2Loader.h
    main/loaders/SNSFLoader.h
    main/loaders/SPCLoader.h
    main/BytePattern.h
    main/common.h
    main/DataSeg.h
    main/datetime.h
    main/DLSFile.h
    main/ExtensionDiscriminator.h
    main/Format.h
    main/helper.h
    main/Loader.h
    main/LogItem.h
    main/Loop.h
    main/Matcher.h
    main/Menu.h
    main/MidiFile.h
    main/Options.h
    main/PSFFile.h
    main/RawFile.h
    main/RiffFile.h
    main/Root.h
    main/ScaleConversion.h
    main/Scanner.h
    main/SeqEvent.h
    main/SeqNote.h
    main/SeqTrack.h
    main/SeqVoiceAllocator.h
    main/SF2File.h
    main/SynthFile.h
    main/VGMColl.h
    main/VGMFile.h
    main/VGMInstrSet.h
    main/VGMItem.h
    main/VGMMiscFile.h
    main/VGMRgn.h
    main/VGMSamp.h
    main/VGMSampColl.h
    main/VGMSeq.h
    main/VGMSeqNoTrks.h
    main/VGMTag.h
    ui/qt/pch.h
    ui/qt/osdepend.h
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
    lib/minizip/crypt.h
    lib/minizip/ioapi.h
    lib/minizip/mztools.h
    lib/minizip/unzip.h
    lib/minizip/zip.h
    lib/tinyxml/src/tinystr.h
    lib/tinyxml/src/tinyxml.h
    lib/tinyxml/tinystr.h
    lib/tinyxml/tinyxml.h
    lib/zlib/include/ioapi.h
    lib/zlib/include/unzip.h
    lib/zlib/include/zconf.h
    lib/zlib/include/zlib.h
    ui/qt/QtVGMRoot.h
    ui/qt/VGMFileListView.h
    ui/qt/RawFileListView.h
    ui/qt/VGMCollListView.h
    ui/qt/MusicPlayer.h

    ui/qt/pch.h
)


qt5_add_resources(UI_RESOURCES ui/qt/resources/resources.qrc)

add_executable(vgmtrans ${SOURCE_FILES} ${UI_RESOURCES})

# The Qt5Widgets_LIBRARIES variable also includes QtGui and QtCore
target_link_libraries(vgmtrans ${Qt5Widgets_LIBRARIES})

find_package (ZLIB)
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
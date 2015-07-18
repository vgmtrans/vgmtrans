cmake_minimum_required(VERSION 2.8.11)
project(VGMTrans)

IF(APPLE)
    SET(CMAKE_LIBRARY_ARCHITECTURE "x86_64-apple-darwin10")
    SET(CMAKE_OSX_ARCHITECTURES "x86_64;")
    SET(GUI_TYPE MACOSX_BUNDLE)
ENDIF(APPLE)

IF(WIN32)
#    SET(CMAKE_LIBRARY_ARCHITECTURE "x86_64-w64-mingw32")
    SET(CMAKE_LIBRARY_ARCHITECTURE "i686-w64-mingw32")
    SET(GUI_TYPE WIN32)
ENDIF(WIN32)

#set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR})

set(CMAKE_PREFIX_PATH
    ${CMAKE_PREFIX_PATH}
    "${CMAKE_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}")
message(STATUS "PREFIX PATH: " ${CMAKE_PREFIX_PATH})

set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${CMAKE_SOURCE_DIR}/cmake)


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
## We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
add_definitions(${Qt5Widgets_DEFINITIONS})
## Executables fail to build with Qt 5 in the default configuration
## without -fPIE. We add that here.
set(CMAKE_CXX_FLAGS "${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")

# Find the QtWidgets library
find_package(Qt5Widgets REQUIRED)


if(MSVC)
  set(CMAKE_CXX_FLAGS "/EHsc")
else()
  #set ( CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libstdc++ -g3 -Wall -O0" )
  set ( CMAKE_CXX_FLAGS "-std=c++11 -w -g3 -Wall -O0" )
  #-Wl,-subsystem,windows
  #set ( CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libc++ -g3 -Wall -O0" )
endif()


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

    "contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include"
    "contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include/glib-2.0"
    "contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include/fluidsynth"
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
    main/formats/HeartBeatPS1Scanner.cpp
    main/formats/HeartBeatPS1Scanner.h
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
    main/formats/NamcoSnesInstr.cpp
    main/formats/NamcoSnesInstr.h
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
    main/formats/NeverlandSnesFormat.h
    main/formats/NeverlandSnesScanner.cpp
    main/formats/NeverlandSnesScanner.h
    main/formats/NeverlandSnesSeq.cpp
    main/formats/NeverlandSnesSeq.h
    main/formats/NinSnesFormat.h
    main/formats/NinSnesInstr.cpp
    main/formats/NinSnesInstr.h
    main/formats/NinSnesScanner.cpp
    main/formats/NinSnesScanner.h
    main/formats/NinSnesSeq.cpp
    main/formats/NinSnesSeq.h
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
    main/formats/PrismSnesFormat.h
    main/formats/PrismSnesInstr.cpp
    main/formats/PrismSnesInstr.h
    main/formats/PrismSnesScanner.cpp
    main/formats/PrismSnesScanner.h
    main/formats/PrismSnesSeq.cpp
    main/formats/PrismSnesSeq.h
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
    main/formats/TamSoftPS1Instr.cpp
    main/formats/TamSoftPS1Instr.h
    main/formats/TamSoftPS1Format.h
    main/formats/TamSoftPS1Scanner.cpp
    main/formats/TamSoftPS1Scanner.h
    main/formats/TamSoftPS1Seq.cpp
    main/formats/TamSoftPS1Seq.h
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
    main/SeqSlider.cpp
    main/SeqSlider.h
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

add_executable(vgmtrans ${GUI_TYPE} ${SOURCE_FILES} ${UI_RESOURCES})

SET_TARGET_PROPERTIES(vgmtrans PROPERTIES LINK_SEARCH_START_STATIC 1)
SET_TARGET_PROPERTIES(vgmtrans PROPERTIES LINK_SEARCH_END_STATIC 1)


# Use the Widgets module from Qt 5.
target_link_libraries(vgmtrans Qt5::Widgets)

if (MSVC)
    #set(ZLIB_INCLUDE_DIR "lib/zlib/include")
    #set(ZLIB_LIBRARY "lib/zlib/lib/win32")
    #FIND_PATH(ZLIB_INCLUDE_DIR zlib.h)
    #FIND_PATH(ZLIB_LIBRARY zlibstat.lib)

    set(WIN_LIBS

        ${CMAKE_SOURCE_DIR}/lib/zlib/lib/win32/zlibstat.lib
        ${CMAKE_SOURCE_DIR}/lib/fluidsynth/lib/win32/libfluidsynthstat.lib
        ${CMAKE_SOURCE_DIR}/lib/glib/lib/win32/glib-2.0.lib
        ${CMAKE_SOURCE_DIR}/lib/glib/lib/win32/gthread-2.0.lib
        winmm.lib
        ws2_32.lib
        dsound.lib
    )
    #target_link_libraries(vgmtrans ${WIN_LIBS})

    target_link_libraries(vgmtrans ${CMAKE_SOURCE_DIR}/lib/zlib/lib/win32/zlibstat.lib)
    target_link_libraries(vgmtrans ${CMAKE_SOURCE_DIR}/lib/fluidsynth/lib/win32/libfluidsynthstat.lib)
    target_link_libraries(vgmtrans ${CMAKE_SOURCE_DIR}/lib/glib/lib/win32/glib-2.0.lib)
    target_link_libraries(vgmtrans ${CMAKE_SOURCE_DIR}/lib/glib/lib/win32/gthread-2.0.lib)

	#LIST(APPEND SOURCE_FILES )
	#set(ADDITIONAL_WIN_SOURCE_FILES
	#	lib/glib/include/glibconfig.h
	#	lib/glib/include/glib/gmacros.h
	#	lib/glib/include/glib/gtypes.h
	#)

    add_definitions(-DVGMTRANS_FOR_QT -DZLIB_WINAPI)

else (MSVC)
    find_package (ZLIB REQUIRED)
    message(STATUS "ZLIB INCLUDE DIR: " ${ZLIB_INCLUDE_DIR})
    message(STATUS "ZLIB LIB DIR: " ${ZLIB_LIBRARY})
    if (ZLIB_FOUND)
      include_directories(${ZLIB_INCLUDE_DIRS})
      target_link_libraries (vgmtrans ${ZLIB_LIBRARIES})
    endif (ZLIB_FOUND)


    find_package (FluidSynth REQUIRED)
    target_link_libraries(vgmtrans ${FLUIDSYNTH_LIBRARIES})

    find_package(GLIB REQUIRED)
    target_link_libraries(vgmtrans ${GLIB_LIBRARIES})

    FIND_LIBRARY(INTL_LIBRARY NAMES intl)
    target_link_libraries(vgmtrans ${INTL_LIBRARY})

    FIND_LIBRARY(ICONV_LIBRARIES NAMES iconv)
    target_link_libraries(vgmtrans ${ICONV_LIBRARIES})

	add_definitions(-DIOAPI_NO_64)
endif (MSVC)

IF (WIN32)
    FIND_LIBRARY(QWINDOWS_LIBRARY qwindows)
    TARGET_LINK_LIBRARIES(vgmtrans
     ${QWINDOWS_LIBRARY}

     imm32.lib
     winmm.lib
     ws2_32.lib
     dsound.lib)

    ADD_DEFINITIONS(-DFLUIDSYNTH_NOT_A_DLL -DG_OS_WIN32)

ENDIF (WIN32)

IF (APPLE)
   FIND_LIBRARY(IOKIT_LIBRARY IOKit)
   FIND_LIBRARY(HARFBUZZ_LIBRARY harfbuzz)
   FIND_LIBRARY(APPKIT_LIBRARY AppKit)
   FIND_LIBRARY(CARBON_LIBRARY Carbon)
   FIND_LIBRARY(COREMIDI_LIBRARY CoreMIDI)
   FIND_LIBRARY(COREAUDIO_LIBRARY CoreAudio)
   FIND_LIBRARY(AUDIOUNIT_LIBRARY AudioUnit)
   FIND_LIBRARY(COREFOUNDATION_LIBRARY CoreFoundation)
   FIND_LIBRARY(CORESERVICES_LIBRARY CoreServices)

   FIND_LIBRARY(QCOCOA_LIBRARY qcocoa)
   FIND_LIBRARY(QT5PLATFORMSUPPORT_LIBRARY Qt5PlatformSupport)
   FIND_LIBRARY(QT5PRINTSUPPORT_LIBRARY Qt5PrintSupport)
   FIND_LIBRARY(CUPS_LIBRARY cups)

   FIND_LIBRARY(READLINE_LIBRARY readline)
   FIND_LIBRARY(NCURSES_LIBRARY ncurses)

   TARGET_LINK_LIBRARIES(vgmtrans
    ${QCOCOA_LIBRARY}
    ${HARFBUZZ_LIBRARY}
    ${APPKIT_LIBRARY}
    ${CARBON_LIBRARY}
    ${COREMIDI_LIBRARY}
    ${COREAUDIO_LIBRARY}
    ${AUDIOUNIT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${CORESERVICES_LIBRARY}

    ${QT5PLATFORMSUPPORT_LIBRARY}
    ${QT5PRINTSUPPORT_LIBRARY}
    ${CUPS_LIBRARY}
    ${IOKIT_LIBRARY}

    ${READLINE_LIBRARY}
    ${NCURSES_LIBRARY}
    )

ENDIF (APPLE)

add_definitions(-DTIXML_USE_STL)

#find_library (fluidsynth PATHS lib/fluidsynth/lib)
#link_directories(lib/fluidsynth/lib)

#target_link_libraries(vgmtrans ${CMAKE_SOURCE_DIR}/lib/fluidsynth/lib/libfluidsynth.a)

set(SOURCE_FILES
    ${SOURCE_FILES}
        
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/src/tinystr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/src/tinyxml.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/src/tinyxmlerror.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/src/tinyxmlparser.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/minizip/ioapi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/minizip/mztools.c
    ${CMAKE_CURRENT_SOURCE_DIR}/minizip/unzip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/minizip/zip.c

    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/mem_sfloader/mem_sfloader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/mem_sfloader/vgmtrans_fluid_midi.c

    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/audio.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/event.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/gen.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/midi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/misc.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/mod.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/ramsfont.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/seq.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/seqbind.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/settings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/sfont.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/shell.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/synth.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/version.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth/voice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include/fluidsynth.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/mem_sfloader/mem_sfloader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/mem_sfloader/vgmtrans_fluid_midi.h

    #${CMAKE_CURRENT_SOURCE_DIR}/minizip/crypt.h
    #${CMAKE_CURRENT_SOURCE_DIR}/minizip/ioapi.h
    #${CMAKE_CURRENT_SOURCE_DIR}/minizip/mztools.h
    #${CMAKE_CURRENT_SOURCE_DIR}/minizip/unzip.h
    #${CMAKE_CURRENT_SOURCE_DIR}/minizip/zip.h

    #${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/src/tinystr.h
    #${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/src/tinyxml.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/tinystr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml/tinyxml.h

    ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include/ioapi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include/unzip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include/zconf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include/zlib.h
    
    PARENT_SCOPE
)

set(INCLUDE_DIRS
    ${INCLUDE_DIRS}

    ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/minizip
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/include
    ${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/mem_sfloader

    PARENT_SCOPE
)
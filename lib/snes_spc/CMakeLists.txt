cmake_minimum_required(VERSION 3.5)
project(snes_spc)

set(SOURCES dsp.cpp
            SNES_SPC.cpp
            SNES_SPC_misc.cpp
            SNES_SPC_state.cpp
            spc.cpp
            SPC_DSP.cpp
            SPC_Filter.cpp)

add_library(snes_spc STATIC ${SOURCES})
set_property(TARGET snes_spc PROPERTY POSITION_INDEPENDENT_CODE ON)

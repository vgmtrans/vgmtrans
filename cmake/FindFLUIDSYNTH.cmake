# VGMTrans (c) 2018
# Licensed under the zlib license
# Check the included LICENSE.txt for details
#=============================================================================
# Copyright 2018 Christophe Giboudeaux <christophe@krop.fr>
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#=============================================================================
# FindFLUIDSYNTH.cmake
# Locate FluidSynth headers and libraries
#  FLUIDSYNTH_INCLUDE_DIR    - Path to the FluidSynth headers
#  FLUIDSYNTH_LIBRARIES      - Path to the FluidSynth libraries
#  FLUIDSYNTH_FOUND          - The system has FluidSynth
#  FLUIDSYNTH_VERSION_STRING - FluidSynth version present on the system

find_path(FLUIDSYNTH_INCLUDE_DIR
        NAMES fluidsynth.h
)

find_library(FLUIDSYNTH_LIBRARIES
        NAMES fluidsynth
)

if(EXISTS "${FLUIDSYNTH_INCLUDE_DIR}/fluidsynth/version.h")
    file(STRINGS "${FLUIDSYNTH_INCLUDE_DIR}/fluidsynth/version.h" _FLUIDSYNTH_VERSION_H
            REGEX "^#define[ ]+FLUIDSYNTH_VERSION[ ]+\"[0-9].[0-9].[0-9]\"")
    string(REGEX REPLACE "^#define[ ]+FLUIDSYNTH_VERSION[ ]+\"([0-9].[0-9].[0-9])\".*" "\\1" FLUIDSYNTH_VERSION_STRING "${_FLUIDSYNTH_VERSION_H}")
    unset(_FLUIDSYNTH_VERSION_H)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FluidSynth
        FOUND_VAR FLUIDSYNTH_FOUND
        REQUIRED_VARS FLUIDSYNTH_LIBRARIES FLUIDSYNTH_INCLUDE_DIR
        VERSION_VAR FLUIDSYNTH_VERSION_STRING
)

mark_as_advanced(
        FLUIDSYNTH_LIBRARIES FLUIDSYNTH_INCLUDE_DIR
)

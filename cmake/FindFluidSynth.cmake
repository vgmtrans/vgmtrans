#.rst:
# FindFluidSynth
# ---------
#
# Try to find the FluidSynth library.
#
# This will define the following variables:
#
# ``FluidSynth_FOUND``
#     System has FluidSynth.
#
# ``FluidSynth_VERSION``
#     The version of FluidSynth.
#
# ``FluidSynth_INCLUDE_DIRS``
#     This should be passed to target_include_directories() if
#     the target is not used for linking.
#
# ``FluidSynth_LIBRARIES``
#     The FluidSynth library.
#     This can be passed to target_link_libraries() instead of
#     the ``FluidSynth::FluidSynth`` target
#
# If ``FluidSynth_FOUND`` is TRUE, the following imported target
# will be available:
#
# ``FluidSynth::FluidSynth``
#     The FluidSynth library
#
#=============================================================================
# Copyright 2018 Christophe Giboudeaux <christophe@krop.fr>
#
#
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

if(NOT FluidSynth_FIND_VERSION)
  set(FluidSynth_FIND_VERSION "1.1.6")
endif()

find_package(PkgConfig QUIET)
pkg_check_modules(PC_FluidSynth QUIET fluidsynth>=${FluidSynth_FIND_VERSION})

find_library(FluidSynth_LIBRARIES
        NAMES fluidsynth libfluidsynth
        HINTS ${PC_FluidSynth_LIBDIR}
        )

find_path(FluidSynth_INCLUDE_DIRS
        NAMES fluidsynth.h
        HINTS ${PC_FluidSynth_INCLUDEDIR}
        )

set(FluidSynth_VERSION "${PC_FluidSynth_VERSION}")

if(NOT FluidSynth_VERSION)
  if(EXISTS "${FluidSynth_INCLUDE_DIRS}/fluidsynth/version.h")
    file(STRINGS "${FluidSynth_INCLUDE_DIRS}/fluidsynth/version.h" _FLUIDSYNTH_VERSION_H
            REGEX "^#define[ ]+FLUIDSYNTH_VERSION.*$")
    string(REGEX REPLACE "^#define[ ]+FLUIDSYNTH_VERSION[ ]+\"([0-9]+.[0-9]+.[0-9]+)\".*" "\\1" FluidSynth_VERSION "${_FLUIDSYNTH_VERSION_H}")
    unset(_FLUIDSYNTH_VERSION_H)
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FluidSynth
        FOUND_VAR FluidSynth_FOUND
        REQUIRED_VARS FluidSynth_LIBRARIES FluidSynth_INCLUDE_DIRS
        VERSION_VAR FluidSynth_VERSION
        )

if(FluidSynth_FOUND AND NOT TARGET FluidSynth::FluidSynth)
  add_library(FluidSynth::FluidSynth UNKNOWN IMPORTED)
  set_target_properties(FluidSynth::FluidSynth PROPERTIES
          IMPORTED_LOCATION "${FluidSynth_LIBRARIES}"
          INTERFACE_INCLUDE_DIRECTORIES "${FluidSynth_INCLUDE_DIRS}"
          )
endif()

include(FeatureSummary)
set_package_properties(FluidSynth PROPERTIES
        URL "https://www.fluidsynth.org/"
        DESCRIPTION "A SoundFont Synthesizer"
        )

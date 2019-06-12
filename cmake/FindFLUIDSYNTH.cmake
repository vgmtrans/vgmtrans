# FindFluidSynth
# ---------
#
# Try to find the FluidSynth library.
#
# This will define the following variables:
#
# ``FLUIDSYNTH_FOUND``
#     System has FluidSynth.
#
# ``FLUIDSYNTH_VERSION``
#     The version of FluidSynth.
#
# ``FLUIDSYNTH_INCLUDE_DIRS``
#     This should be passed to target_include_directories() if
#     the target is not used for linking.
#
# ``FLUIDSYNTH_LIBRARIES``
#     The FluidSynth library.
#     This can be passed to target_link_libraries() instead of
#     the ``FluidSynth::FluidSynth`` target
#
# If ``FLUIDSYNTH_FOUND`` is TRUE, the following imported target
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

if(NOT ${FluidSynth_FIND_VERSION})
  set(FluidSynth_FIND_VERSION "1.1.6")
endif()

find_package(PkgConfig QUIET)
pkg_check_modules(PC_FluidSynth QUIET fluidsynth>=${FluidSynth_FIND_VERSION})

find_library(FLUIDSYNTH_LIBRARIES
  NAMES fluidsynth
  HINTS ${PC_FluidSynth_LIBDIR}
)

find_path(FLUIDSYNTH_INCLUDE_DIRS
  NAMES fluidsynth.h
  HINTS ${PC_FluidSynth_INCLUDEDIR}
)

if(NOT FLUIDSYNTH_VERSION)
  if(EXISTS "${FLUIDSYNTH_INCLUDE_DIRS}/fluidsynth/version.h")
    file(STRINGS "${FLUIDSYNTH_INCLUDE_DIRS}/fluidsynth/version.h" _FLUIDSYNTH_VERSION_H
         REGEX "^#define[ ]+FLUIDSYNTH_VERSION[ ]+\"[0-9].[0-9].[0-9]\"")
    string(REGEX REPLACE "^#define[ ]+FLUIDSYNTH_VERSION[ ]+\"([0-9].[0-9].[0-9])\".*" "\\1" FLUIDSYNTH_VERSION "${_FLUIDSYNTH_VERSION_H}")
    unset(_FLUIDSYNTH_VERSION_H)
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(fluidsynth
  FOUND_VAR FLUIDSYNTH_FOUND
  REQUIRED_VARS FLUIDSYNTH_LIBRARIES FLUIDSYNTH_INCLUDE_DIRS
  VERSION_VAR FLUIDSYNTH_VERSION
)

if(FLUIDSYNTH_FOUND AND NOT TARGET fluidsynth)
  add_library(fluidsynth UNKNOWN IMPORTED)
  set_target_properties(fluidsynth PROPERTIES
    IMPORTED_LOCATION "${FLUIDSYNTH_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${FLUIDSYNTH_INCLUDE_DIRS}"
  )
endif()

include(FeatureSummary)
set_package_properties(fluidsynth PROPERTIES
  URL "http://www.fluidsynth.org/"
  DESCRIPTION "A SoundFont Synthesizer"
)

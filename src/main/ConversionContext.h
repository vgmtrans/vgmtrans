/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "ConversionTypes.h"
#include "ModSourceMap.h"

class ConversionOptions;

struct ConversionContext {
  ConversionContext();
  ConversionContext(BankSelectStyle bankSelectStyle,
                    int sequenceLoops,
                    bool skipChannel10,
                    const ModSourceMap& sf2ModSources,
                    const ModSourceMap& dlsModSources,
                    SynthTarget midiModulationSynthTarget);

  static ConversionContext fromOptions(const ConversionOptions& options, SynthTarget midiModulationSynthTarget);

  [[nodiscard]] const ModSourceMap& modSourceMap(SynthTarget target) const;
  [[nodiscard]] ModSource midiSourceFor(ModDest destination) const;
  [[nodiscard]] ModSource synthSourceFor(SynthTarget target, ModDest destination) const;

  BankSelectStyle bankSelectStyle;
  int sequenceLoops;
  bool skipChannel10;
  ModSourceMap sf2ModSources;
  ModSourceMap dlsModSources;
  SynthTarget midiModulationSynthTarget;
};

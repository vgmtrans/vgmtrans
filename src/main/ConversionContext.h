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
                    ModulationSourceTarget midiModulationTarget);

  static ConversionContext fromOptions(const ConversionOptions& options, ModulationSourceTarget midiModulationTarget);

  [[nodiscard]] const ModSourceMap& modSourceMap(ModulationSourceTarget target) const;
  [[nodiscard]] ModSource midiSourceFor(ModDest destination) const;
  [[nodiscard]] ModSource synthSourceFor(ModulationSourceTarget target, ModDest destination) const;

  BankSelectStyle bankSelectStyle;
  int sequenceLoops;
  bool skipChannel10;
  ModSourceMap sf2ModSources;
  ModSourceMap dlsModSources;
  ModulationSourceTarget midiModulationTarget;
};

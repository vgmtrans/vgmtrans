/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ConversionContext.h"
#include "Options.h"

ConversionContext::ConversionContext()
    : bankSelectStyle(BankSelectStyle::GS),
      sequenceLoops(0),
      skipChannel10(true),
      sf2ModSources(ModulationSourceTarget::SoundFont),
      dlsModSources(ModulationSourceTarget::DLS),
      midiModulationTarget(ModulationSourceTarget::SoundFont) {}

ConversionContext::ConversionContext(BankSelectStyle bankSelectStyle,
                                     int sequenceLoops,
                                     bool skipChannel10,
                                     const ModSourceMap& sf2ModSources,
                                     const ModSourceMap& dlsModSources,
                                     ModulationSourceTarget midiModulationTarget)
    : bankSelectStyle(bankSelectStyle),
      sequenceLoops(sequenceLoops),
      skipChannel10(skipChannel10),
      sf2ModSources(sf2ModSources),
      dlsModSources(dlsModSources),
      midiModulationTarget(midiModulationTarget) {}

ConversionContext ConversionContext::fromOptions(
    const ConversionOptions& options,
    ModulationSourceTarget midiModulationTarget) {
  return {
      options.bankSelectStyle(),
      options.numSequenceLoops(),
      options.skipChannel10(),
      options.modSourceMap(ModulationSourceTarget::SoundFont),
      options.modSourceMap(ModulationSourceTarget::DLS),
      midiModulationTarget,
  };
}

const ModSourceMap& ConversionContext::modSourceMap(ModulationSourceTarget target) const {
  return target == ModulationSourceTarget::DLS ? dlsModSources : sf2ModSources;
}

ModSource ConversionContext::midiSourceFor(ModDest destination) const {
  return modSourceMap(midiModulationTarget).sourceFor(destination);
}

ModSource ConversionContext::synthSourceFor(ModulationSourceTarget target, ModDest destination) const {
  return modSourceMap(target).sourceFor(destination);
}

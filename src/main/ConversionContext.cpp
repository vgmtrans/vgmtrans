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
      sf2ModSources(SynthTarget::SoundFont),
      dlsModSources(SynthTarget::DLS),
      midiModulationSynthTarget(SynthTarget::SoundFont) {}

ConversionContext::ConversionContext(BankSelectStyle bankSelectStyle,
                                     int sequenceLoops,
                                     bool skipChannel10,
                                     const ModSourceMap& sf2ModSources,
                                     const ModSourceMap& dlsModSources,
                                     SynthTarget midiModulationSynthTarget)
    : bankSelectStyle(bankSelectStyle),
      sequenceLoops(sequenceLoops),
      skipChannel10(skipChannel10),
      sf2ModSources(sf2ModSources),
      dlsModSources(dlsModSources),
      midiModulationSynthTarget(midiModulationSynthTarget) {}

ConversionContext ConversionContext::fromOptions(
    const ConversionOptions& options,
    SynthTarget midiModulationSynthTarget) {
  return {
      options.bankSelectStyle(),
      options.numSequenceLoops(),
      options.skipChannel10(),
      options.modSourceMap(SynthTarget::SoundFont),
      options.modSourceMap(SynthTarget::DLS),
      midiModulationSynthTarget,
  };
}

const ModSourceMap& ConversionContext::modSourceMap(SynthTarget target) const {
  return target == SynthTarget::DLS ? dlsModSources : sf2ModSources;
}

ModSource ConversionContext::midiSourceFor(ModDest destination) const {
  return modSourceMap(midiModulationSynthTarget).sourceFor(destination);
}

ModSource ConversionContext::synthSourceFor(SynthTarget target, ModDest destination) const {
  return modSourceMap(target).sourceFor(destination);
}

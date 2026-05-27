/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Types.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include "ConversionContext.h"
#include "SF2File.h"
#include "version.h"
#include "VGMInstrSet.h"
#include "SynthFile.h"
#include "ScaleConversion.h"
#include "Root.h"
#include "VGMRgn.h"

namespace {

constexpr double kEmu8000InitialAttenuationScale = 2.5;
constexpr double kSoundFontCentibelsPerDecibel = 10.0;
constexpr double kSoundFontMaxInitialAttenuationCentibels = 1440.0;

std::optional<SFModulator> sf2SourceForModSource(ModSource source) {
  constexpr u16 midiContinuousController = 1u << 7;
  constexpr u16 bipolar = 1u << 9;

  if (auto controller = midiControllerForModSource(source)) {
    return static_cast<SFModulator>(midiContinuousController | *controller);
  }

  switch (source) {
    case ModSource::ChannelPressure:
      return 13;
    case ModSource::PolyPressure:
      return 10;
    case ModSource::PitchWheel:
      return static_cast<SFModulator>(bipolar | 14);
    case ModSource::None:
      return std::nullopt;
    default:
      break;
  }
  return std::nullopt;
}

ModSource sourceForModulator(const SynthModulator& modulator, const ConversionContext& context) {
  return modulator.source.value_or(context.synthSourceFor(SynthTarget::SoundFont, modulator.destination));
}

std::optional<SFModulator> sf2SourceForModulator(const SynthModulator& modulator, const ConversionContext& context) {
  return sf2SourceForModSource(sourceForModulator(modulator, context));
}

SFGenerator sf2GeneratorForModDestination(ModDest destination) {
  switch (destination) {
    case ModDest::VibLfoToPitch:
      return vibLfoToPitch;
    case ModDest::VibLfoFreq:
      return freqVibLFO;
    case ModDest::VibLfoDelay:
      return delayVibLFO;
    case ModDest::ModLfoToVol:
      return modLfoToVolume;
    case ModDest::ModLfoFreq:
      return freqModLFO;
    case ModDest::ModLfoDelay:
      return delayModLFO;
    case ModDest::InitialAtten:
      return initialAttenuation;
  }
  return endOper;
}

s16 sf2AmountForModulator(const SynthModulator& modulator) {
  return static_cast<s16>(std::clamp<s32>(
      modulator.amount, std::numeric_limits<s16>::min(), std::numeric_limits<s16>::max()));
}

s16 sf2AmountForGenerator(const SynthGenerator& generator) {
  return static_cast<s16>(std::clamp<s32>(
      generator.amount, std::numeric_limits<s16>::min(), std::numeric_limits<s16>::max()));
}

size_t numSf2ModulatorsForInstr(const SynthInstr* instr, const ConversionContext& context) {
  return std::ranges::count_if(instr->modulators(), [&context](const auto& modulator) {
    return sf2SourceForModulator(modulator, context).has_value();
  });
}

bool hasInstrumentGlobalZone(const SynthInstr* instr, const ConversionContext& context) {
  return !instr->generators().empty() || numSf2ModulatorsForInstr(instr, context) != 0;
}

} // namespace

SF2InfoListChunk::SF2InfoListChunk(const std::string& name)
    : LISTChunk("INFO") {
  // Create a date string
  time_t current_time = time(nullptr);
  char *c_time_string = ctime(&current_time);

  // Add the child info chunks
  Chunk *ifilCk = new Chunk("ifil");
  sfVersionTag versionTag;        //soundfont version 2.01
  versionTag.wMajor = 2;
  versionTag.wMinor = 1;
  ifilCk->setData(&versionTag, sizeof(versionTag));
  addChildChunk(ifilCk);
  addChildChunk(new SF2StringChunk("isng", "EMU8000"));
  addChildChunk(new SF2StringChunk("INAM", name));
  addChildChunk(new SF2StringChunk("ICRD", std::string(c_time_string)));
  addChildChunk(new SF2StringChunk("ISFT", std::string("VGMTrans " + std::string(VGMTRANS_VERSION))));
}


//  *******
//  SF2File
//  *******

int SF2File::numOfGeneratorsForRgn(SynthRgn* rgn) {
  int numOfGenerators = 14;
  if (rgn->lfoVibDepthCents() > 0 && rgn->lfoVibFreqHz() > 0) {
    numOfGenerators += 2;
    if (rgn->lfoVibDelaySeconds() > 0)
      numOfGenerators += 1;
  }
  return numOfGenerators;
}

SF2File::SF2File(SynthFile* synthfile, const ConversionContext& context)
    : RiffFile(synthfile->m_name, "sfbk") {

  //***********
  // INFO chunk
  //***********
  addChildChunk(new SF2InfoListChunk(name));

  // sdta chunk and its child smpl chunk containing all samples
  LISTChunk *sdtaCk = new LISTChunk("sdta");
  Chunk *smplCk = new Chunk("smpl");

  // Concatanate all of the samples together and add the result to the smpl chunk data
  size_t numWaves = synthfile->vWaves.size();
  u32 smplCkSize = 0;
  for (size_t i = 0; i < numWaves; i++) {
    SynthWave *wave = synthfile->vWaves[i];
    smplCkSize += wave->dataSize + (46 * 2);    // plus the 46 padding samples required by sf2 spec
  }
  smplCk->setSize(smplCkSize);
  smplCk->data = new u8[smplCkSize];
  u32 bufPtr = 0;
  for (size_t i = 0; i < numWaves; i++) {
    SynthWave *wave = synthfile->vWaves[i];

    memcpy(smplCk->data + bufPtr, wave->data.data(), wave->dataSize);
    memset(smplCk->data + bufPtr + wave->dataSize, 0, 46 * 2);
    bufPtr += wave->dataSize + (46 * 2);        // plus the 46 padding samples required by sf2 spec
  }

  sdtaCk->addChildChunk(smplCk);
  this->addChildChunk(sdtaCk);

  //***********
  // pdta chunk
  //***********

  LISTChunk *pdtaCk = new LISTChunk("pdta");

  //***********
  // phdr chunk
  //***********
  Chunk *phdrCk = new Chunk("phdr");
  size_t numInstrs = synthfile->vInstrs.size();
  phdrCk->setSize(static_cast<u32>((numInstrs + 1) * sizeof(sfPresetHeader)));
  phdrCk->data = new u8[phdrCk->size()];

  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    sfPresetHeader presetHdr{};
    memcpy(presetHdr.achPresetName, instr->name.c_str(), std::min(instr->name.length(), static_cast<size_t>(20)));
    presetHdr.wPreset = static_cast<u16>(instr->ulInstrument);

    // Despite being a 16-bit value, SF2 only supports banks up to 128. Since
    // it's pretty common to have either MSB or LSB be 0, we'll use MSB if the
    // value is greater than 128
    u16 bank16 = static_cast<u16>(instr->ulBank);

    if (bank16 > 128) {
      presetHdr.wBank = (bank16 >> 8) & 0x7F;
    } else {
      presetHdr.wBank = bank16;
    }
    presetHdr.wPresetBagNdx = static_cast<u16>(i);
    presetHdr.dwLibrary = 0;
    presetHdr.dwGenre = 0;
    presetHdr.dwMorphology = 0;

    memcpy(phdrCk->data + (i * sizeof(sfPresetHeader)), &presetHdr, sizeof(sfPresetHeader));
  }
  //  add terminal sfPresetBag
  sfPresetHeader presetHdr{};
  presetHdr.wPresetBagNdx = static_cast<u16>(numInstrs);
  memcpy(phdrCk->data + (numInstrs * sizeof(sfPresetHeader)), &presetHdr, sizeof(sfPresetHeader));
  pdtaCk->addChildChunk(phdrCk);

  //***********
  // pbag chunk
  //***********
  Chunk *pbagCk = new Chunk("pbag");
  constexpr size_t ITEMS_IN_PGEN = 2;
  pbagCk->setSize(static_cast<u32>((numInstrs + 1) * sizeof(sfPresetBag)));
  pbagCk->data = new u8[pbagCk->size()];
  for (size_t i = 0; i < numInstrs; i++) {
    sfPresetBag presetBag{};
    presetBag.wGenNdx = static_cast<u16>(i * ITEMS_IN_PGEN);
    presetBag.wModNdx = 0;

    memcpy(pbagCk->data + (i * sizeof(sfPresetBag)), &presetBag, sizeof(sfPresetBag));
  }
  //  add terminal sfPresetBag
  sfPresetBag presetBag{};
  presetBag.wGenNdx = static_cast<u16>(numInstrs * ITEMS_IN_PGEN);
  memcpy(pbagCk->data + (numInstrs * sizeof(sfPresetBag)), &presetBag, sizeof(sfPresetBag));
  pdtaCk->addChildChunk(pbagCk);

  //***********
  // pmod chunk
  //***********
  Chunk *pmodCk = new Chunk("pmod");
  //  create the terminal field
  sfModList modList{};
  pmodCk->setData(&modList, sizeof(sfModList));
  //modList.sfModSrcOper = cc1_Mod;
  //modList.sfModDestOper = startAddrsOffset;
  //modList.modAmount = 0;
  //modList.sfModAmtSrcOper = cc1_Mod;
  //modList.sfModTransOper = linear;
  pdtaCk->addChildChunk(pmodCk);

  //***********
  // pgen chunk
  //***********
  Chunk *pgenCk = new Chunk("pgen");
  //pgenCk->size = (synthfile->vInstrs.size()+1) * sizeof(sfGenList);
  pgenCk->setSize(static_cast<u32>((synthfile->vInstrs.size() * sizeof(sfGenList) * ITEMS_IN_PGEN) + sizeof(sfGenList)));
  pgenCk->data = new u8[pgenCk->size()];
  u32 dataPtr = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    sfGenList genList{};

    // reverbEffectsSend - Value is in 0.1% units, so multiply by 1000. Ex: 250 == 25%.
    genList.sfGenOper = reverbEffectsSend;
    genList.genAmount.shAmount = instr->reverb * 1000;
    memcpy(pgenCk->data + dataPtr, &genList, sizeof(sfGenList));
    dataPtr += sizeof(sfGenList);

    genList.sfGenOper = instrument;
    genList.genAmount.wAmount = static_cast<u16>(i);
    memcpy(pgenCk->data + dataPtr, &genList, sizeof(sfGenList));
    dataPtr += sizeof(sfGenList);
  }
  //  add terminal sfGenList
  sfGenList genList{};
  memcpy(pgenCk->data + dataPtr, &genList, sizeof(sfGenList));

  pdtaCk->addChildChunk(pgenCk);

  //***********
  // inst chunk
  //***********
  Chunk *instCk = new Chunk("inst");
  instCk->setSize(static_cast<u32>((synthfile->vInstrs.size() + 1) * sizeof(sfInst)));
  instCk->data = new u8[instCk->size()];
  u16 instBagCounter = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    sfInst inst{};
    memcpy(inst.achInstName, instr->name.c_str(), std::min(instr->name.length(), static_cast<size_t>(20)));
    inst.wInstBagNdx = instBagCounter;
    instBagCounter += static_cast<u16>(instr->vRgns.size());
    if (hasInstrumentGlobalZone(instr, context)) {
      instBagCounter++;
    }

    memcpy(instCk->data + (i * sizeof(sfInst)), &inst, sizeof(sfInst));
  }
  //  add terminal sfInst
  sfInst inst{};
  inst.wInstBagNdx = instBagCounter;
  memcpy(instCk->data + (numInstrs * sizeof(sfInst)), &inst, sizeof(sfInst));
  pdtaCk->addChildChunk(instCk);

  //***********
  // ibag chunk - stores all zones (regions) for instruments
  //***********
  Chunk *ibagCk = new Chunk("ibag");

  u32 numTotalInstBags = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];
    numTotalInstBags += static_cast<u32>(instr->vRgns.size());
    if (hasInstrumentGlobalZone(instr, context)) {
      numTotalInstBags++;
    }
  }

  ibagCk->setSize((numTotalInstBags + 1) * sizeof(sfInstBag));
  ibagCk->data = new u8[ibagCk->size()];

  instBagCounter = 0;
  int instGenCounter = 0;
  int instModCounter = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    if (hasInstrumentGlobalZone(instr, context)) {
      sfInstBag globalInstBag{};
      globalInstBag.wInstGenNdx = static_cast<u16>(instGenCounter);
      globalInstBag.wInstModNdx = static_cast<u16>(instModCounter);
      instGenCounter += static_cast<int>(instr->generators().size());
      instModCounter += static_cast<int>(numSf2ModulatorsForInstr(instr, context));
      memcpy(ibagCk->data + (instBagCounter++ * sizeof(sfInstBag)), &globalInstBag, sizeof(sfInstBag));
    }

    size_t numRgns = instr->vRgns.size();
    for (size_t j = 0; j < numRgns; j++) {
      sfInstBag instBag{};
      instBag.wInstGenNdx = static_cast<u16>(instGenCounter);
      instGenCounter += numOfGeneratorsForRgn(instr->vRgns[j]);
      instBag.wInstModNdx = static_cast<u16>(instModCounter);

      memcpy(ibagCk->data + (instBagCounter++ * sizeof(sfInstBag)), &instBag, sizeof(sfInstBag));
    }
  }
  //  add terminal sfInstBag
  sfInstBag instBag{};
  instBag.wInstGenNdx = static_cast<u16>(instGenCounter);
  instBag.wInstModNdx = static_cast<u16>(instModCounter);
  memcpy(ibagCk->data + (instBagCounter * sizeof(sfInstBag)), &instBag, sizeof(sfInstBag));
  pdtaCk->addChildChunk(ibagCk);

  //***********
  // imod chunk
  //***********
  u32 numTotalMods = 1;
  for (const auto instr : synthfile->vInstrs) {
    if (hasInstrumentGlobalZone(instr, context)) {
      numTotalMods += static_cast<u32>(numSf2ModulatorsForInstr(instr, context));
    }
  }

  Chunk *imodCk = new Chunk("imod");
  imodCk->setSize(numTotalMods * sizeof(sfInstModList));
  imodCk->data = new u8[imodCk->size()];
  dataPtr = 0;
  for (const auto instr : synthfile->vInstrs) {
    for (const auto& modulator : instr->modulators()) {
      const auto source = sf2SourceForModulator(modulator, context);
      if (!source.has_value()) {
        continue;
      }

      sfInstModList instModList{};
      instModList.sfModSrcOper = *source;
      instModList.sfModDestOper = sf2GeneratorForModDestination(modulator.destination);
      instModList.modAmount = sf2AmountForModulator(modulator);
      instModList.sfModAmtSrcOper = 0;
      instModList.sfModTransOper = linear;
      memcpy(imodCk->data + dataPtr, &instModList, sizeof(sfInstModList));
      dataPtr += sizeof(sfInstModList);
    }
  }
  sfInstModList instModList{};
  memcpy(imodCk->data + dataPtr, &instModList, sizeof(sfInstModList));
  pdtaCk->addChildChunk(imodCk);

  //***********
  // igen chunk
  //***********
  u32 numTotalGens = 1;
  for (const auto instr : synthfile->vInstrs) {
    numTotalGens += static_cast<u32>(instr->generators().size());
    for (const auto rgn : instr->vRgns) {
      numTotalGens += numOfGeneratorsForRgn(rgn);
    }
  }

  Chunk *igenCk = new Chunk("igen");
  igenCk->setSize(numTotalGens * sizeof(sfInstGenList));
  igenCk->data = new u8[igenCk->size()];
  dataPtr = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    for (const auto& generator : instr->generators()) {
      sfInstGenList instGenList{};
      instGenList.sfGenOper = sf2GeneratorForModDestination(generator.destination);
      instGenList.genAmount.shAmount = sf2AmountForGenerator(generator);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);
    }

    size_t numRgns = instr->vRgns.size();
    for (size_t j = 0; j < numRgns; j++) {
      SynthRgn *rgn = instr->vRgns[j];

      sfInstGenList instGenList;
      // Key range. This must be the first chunk
      instGenList.sfGenOper = keyRange;
      instGenList.genAmount.ranges.byLo = static_cast<u8>(rgn->usKeyLow);
      instGenList.genAmount.ranges.byHi = static_cast<u8>(rgn->usKeyHigh);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // Velocity range. This must be the next chunk
      instGenList.sfGenOper = velRange;
      instGenList.genAmount.ranges.byLo = static_cast<u8>(rgn->usVelLow);
      instGenList.genAmount.ranges.byHi = static_cast<u8>(rgn->usVelHigh);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // initialAttenuation
      instGenList.sfGenOper = initialAttenuation;
      // INFO.isng declares EMU8000. EMU-compatible SF2 synths apply a 0.4
      // factor to generator 48, so store the reciprocal to preserve SynthFile's
      // dB attenuation.
      u16 atten = static_cast<u16>(std::clamp(
        std::round((rgn->attenDb + rgn->sampinfo->attenuation) *
                   kSoundFontCentibelsPerDecibel *
                   kEmu8000InitialAttenuationScale),
        0.0, kSoundFontMaxInitialAttenuationCentibels));
      instGenList.genAmount.wAmount = atten;
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // pan
      instGenList.sfGenOper = pan;
      instGenList.genAmount.shAmount = static_cast<s16>(convertPercentPanTo10thPercentUnits(rgn->art->pan));
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // sampleModes
      instGenList.sfGenOper = sampleModes;
      instGenList.genAmount.wAmount = rgn->sampinfo->cSampleLoops;
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // overridingRootKey
      instGenList.sfGenOper = overridingRootKey;
      instGenList.genAmount.wAmount = rgn->sampinfo->usUnityNote;
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // coarseTune
      instGenList.sfGenOper = coarseTune;
      instGenList.genAmount.shAmount = rgn->coarseTuneSemitones;
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // fineTune
      instGenList.sfGenOper = fineTune;
      instGenList.genAmount.shAmount = rgn->fineTuneCents;
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // attackVolEnv
      instGenList.sfGenOper = attackVolEnv;
      instGenList.genAmount.shAmount = secondsToSf2Timecents(rgn->art->attack_time);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // holdVolEnv
      instGenList.sfGenOper = holdVolEnv;
      instGenList.genAmount.shAmount = secondsToSf2Timecents(rgn->art->hold_time);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // decayVolEnv
      instGenList.sfGenOper = decayVolEnv;
      instGenList.genAmount.shAmount = secondsToSf2Timecents(rgn->art->decay_time);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // sustainVolEnv
      instGenList.sfGenOper = sustainVolEnv;
      if (rgn->art->sustain_lev > 100.0)
        rgn->art->sustain_lev = 100.0;
      instGenList.genAmount.shAmount = static_cast<s16>(rgn->art->sustain_lev * 10);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // releaseVolEnv
      instGenList.sfGenOper = releaseVolEnv;
      instGenList.genAmount.shAmount = secondsToSf2Timecents(rgn->art->release_time);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // reverbEffectsSend
      //instGenList.sfGenOper = reverbEffectsSend;
      //instGenList.genAmount.shAmount = 800;
      //memcpy(pgenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      //dataPtr += sizeof(sfInstGenList);

      if (rgn->lfoVibDepthCents() > 0 && rgn->lfoVibFreqHz() > 0) {
        instGenList.sfGenOper = vibLfoToPitch;
        instGenList.genAmount.shAmount = round(rgn->lfoVibDepthCents());
        memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
        dataPtr += sizeof(sfInstGenList);

        instGenList.sfGenOper = freqVibLFO;
        double hz = rgn->lfoVibFreqHz();
        instGenList.genAmount.shAmount = round( 1200 * log2( hz / 8.176 ) );
        memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
        dataPtr += sizeof(sfInstGenList);

        if (rgn->lfoVibDelaySeconds() > 0) {
          instGenList.sfGenOper = delayVibLFO;
          double delaySeconds = rgn->lfoVibDelaySeconds();
          instGenList.genAmount.shAmount = round( 1200 * log2( delaySeconds ) );
          memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
          dataPtr += sizeof(sfInstGenList);
        }
      }

      // sampleID - this is the terminal chunk
      instGenList.sfGenOper = sampleID;
      instGenList.genAmount.wAmount = static_cast<u16>(rgn->tableIndex);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      //int numConnBlocks = rgn->art->vConnBlocks.size();
      //for (int k = 0; k < numConnBlocks; k++)
      //{
      //	SynthConnectionBlock* connBlock = rgn->art->vConnBlocks[k];
      //	connBlock->
      //}
    }
  }
  //  add terminal sfInstBag
  sfInstGenList instGenList{};
  memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
  //memset(ibagCk->data + (numRgns*sizeof(sfInstBag)), 0, sizeof(sfInstBag));
  //igenCk->SetData(&genList, sizeof(sfGenList));
  pdtaCk->addChildChunk(igenCk);

  //***********
  // shdr chunk
  //***********
  Chunk *shdrCk = new Chunk("shdr");

  size_t numSamps = synthfile->vWaves.size();
  shdrCk->setSize(static_cast<u32>((numSamps + 1) * sizeof(sfSample)));
  shdrCk->data = new u8[shdrCk->size()];

  u32 sampOffset = 0;
  for (size_t i = 0; i < numSamps; i++) {
    SynthWave *wave = synthfile->vWaves[i];

    sfSample samp{};
    memcpy(samp.achSampleName, wave->name.c_str(), std::min(wave->name.length(), static_cast<size_t>(20)));
    samp.dwStart = sampOffset;
    samp.dwEnd = samp.dwStart + (wave->dataSize / sizeof(u16));
    sampOffset = samp.dwEnd + 46;        // plus the 46 padding samples required by sf2 spec

    // Search through all regions for an associated sampInfo structure with this sample
    SynthSampInfo *sampInfo = nullptr;
    for (size_t j = 0; j < numInstrs; j++) {
      SynthInstr *instr = synthfile->vInstrs[j];

      size_t numRgns = instr->vRgns.size();
      for (size_t k = 0; k < numRgns; k++) {
        SynthRgn *rgn = instr->vRgns[k];
        if (rgn->tableIndex == i && rgn->sampinfo != nullptr) {
          sampInfo = rgn->sampinfo;
          break;
        }
      }
      if (sampInfo != nullptr)
        break;
    }
    //  If we didn't find a rgn association, then it should be in the SynthWave structure.
    if (sampInfo == nullptr)
      sampInfo = wave->sampinfo;
    assert (sampInfo != NULL);

    assert(sampInfo->sFineTune >= std::numeric_limits<char>::min() &&
           sampInfo->sFineTune <= std::numeric_limits<char>::max());

    samp.dwStartloop = samp.dwStart + sampInfo->ulLoopStart;
    samp.dwEndloop = samp.dwStartloop + sampInfo->ulLoopLength;
    samp.dwSampleRate = wave->dwSamplesPerSec;
    samp.byOriginalKey = static_cast<u8>(sampInfo->usUnityNote) - (sampInfo->sFineTune / 100);
    samp.chCorrection = static_cast<char>(sampInfo->sFineTune % 100);
    samp.wSampleLink = 0;
    samp.sfSampleType = monoSample;

    memcpy(shdrCk->data + (i * sizeof(sfSample)), &samp, sizeof(sfSample));
  }

  //  add terminal sfSample
  memset(shdrCk->data + (numSamps * sizeof(sfSample)), 0, sizeof(sfSample));
  pdtaCk->addChildChunk(shdrCk);

  this->addChildChunk(pdtaCk);
}

std::vector<u8> SF2File::saveToMem() {
  std::vector<u8> buf(size());
  write(buf.data());
  return buf;
}

bool SF2File::saveSF2File(const std::filesystem::path &filepath) {
  auto buf = saveToMem();
  bool result = pRoot->UI_writeBufferToFile(filepath, buf.data(), buf.size());
  return result;
}

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SF2File.h"
#include <algorithm>
#include <cmath>

#include "version.h"
#include "SynthFile.h"
#include "ScaleConversion.h"
#include "Root.h"

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

SF2File::SF2File(SynthFile *synthfile)
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
  uint32_t smplCkSize = 0;
  for (size_t i = 0; i < numWaves; i++) {
    SynthWave *wave = synthfile->vWaves[i];
    smplCkSize += wave->dataSize + (46 * 2);    // plus the 46 padding samples required by sf2 spec
  }
  smplCk->setSize(smplCkSize);
  smplCk->data = new uint8_t[smplCkSize];
  uint32_t bufPtr = 0;
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
  phdrCk->setSize(static_cast<uint32_t>((numInstrs + 1) * sizeof(sfPresetHeader)));
  phdrCk->data = new uint8_t[phdrCk->size()];

  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    sfPresetHeader presetHdr{};
    memcpy(presetHdr.achPresetName, instr->name.c_str(), std::min(instr->name.length(), static_cast<size_t>(20)));
    presetHdr.wPreset = static_cast<uint16_t>(instr->ulInstrument);

    // Despite being a 16-bit value, SF2 only supports banks up to 128. Since
    // it's pretty common to have either MSB or LSB be 0, we'll use MSB if the
    // value is greater than 128
    uint16_t bank16 = static_cast<uint16_t>(instr->ulBank);

    if (bank16 > 128) {
      presetHdr.wBank = (bank16 >> 8) & 0x7F;
    } else {
      presetHdr.wBank = bank16;
    }
    presetHdr.wPresetBagNdx = static_cast<uint16_t>(i);
    presetHdr.dwLibrary = 0;
    presetHdr.dwGenre = 0;
    presetHdr.dwMorphology = 0;

    memcpy(phdrCk->data + (i * sizeof(sfPresetHeader)), &presetHdr, sizeof(sfPresetHeader));
  }
  //  add terminal sfPresetBag
  sfPresetHeader presetHdr{};
  presetHdr.wPresetBagNdx = static_cast<uint16_t>(numInstrs);
  memcpy(phdrCk->data + (numInstrs * sizeof(sfPresetHeader)), &presetHdr, sizeof(sfPresetHeader));
  pdtaCk->addChildChunk(phdrCk);

  //***********
  // pbag chunk
  //***********
  Chunk *pbagCk = new Chunk("pbag");
  constexpr size_t ITEMS_IN_PGEN = 2;
  pbagCk->setSize(static_cast<uint32_t>((numInstrs + 1) * sizeof(sfPresetBag)));
  pbagCk->data = new uint8_t[pbagCk->size()];
  for (size_t i = 0; i < numInstrs; i++) {
    sfPresetBag presetBag{};
    presetBag.wGenNdx = static_cast<uint16_t>(i * ITEMS_IN_PGEN);
    presetBag.wModNdx = 0;

    memcpy(pbagCk->data + (i * sizeof(sfPresetBag)), &presetBag, sizeof(sfPresetBag));
  }
  //  add terminal sfPresetBag
  sfPresetBag presetBag{};
  presetBag.wGenNdx = static_cast<uint16_t>(numInstrs * ITEMS_IN_PGEN);
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
  pgenCk->setSize(static_cast<uint32_t>((synthfile->vInstrs.size() * sizeof(sfGenList) * ITEMS_IN_PGEN) + sizeof(sfGenList)));
  pgenCk->data = new uint8_t[pgenCk->size()];
  uint32_t dataPtr = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    sfGenList genList{};

    // reverbEffectsSend - Value is in 0.1% units, so multiply by 1000. Ex: 250 == 25%.
    genList.sfGenOper = reverbEffectsSend;
    genList.genAmount.shAmount = instr->reverb * 1000;
    memcpy(pgenCk->data + dataPtr, &genList, sizeof(sfGenList));
    dataPtr += sizeof(sfGenList);

    genList.sfGenOper = instrument;
    genList.genAmount.wAmount = static_cast<uint16_t>(i);
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
  instCk->setSize(static_cast<uint32_t>((synthfile->vInstrs.size() + 1) * sizeof(sfInst)));
  instCk->data = new uint8_t[instCk->size()];
  uint16_t rgnCounter = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    sfInst inst{};
    memcpy(inst.achInstName, instr->name.c_str(), std::min(instr->name.length(), static_cast<size_t>(20)));
    inst.wInstBagNdx = rgnCounter;
    rgnCounter += instr->vRgns.size();

    memcpy(instCk->data + (i * sizeof(sfInst)), &inst, sizeof(sfInst));
  }
  //  add terminal sfInst
  sfInst inst{};
  inst.wInstBagNdx = rgnCounter;
  memcpy(instCk->data + (numInstrs * sizeof(sfInst)), &inst, sizeof(sfInst));
  pdtaCk->addChildChunk(instCk);

  //***********
  // ibag chunk - stores all zones (regions) for instruments
  //***********
  Chunk *ibagCk = new Chunk("ibag");

  uint32_t numTotalRgns = 0;
  for (size_t i = 0; i < numInstrs; i++)
    numTotalRgns += synthfile->vInstrs[i]->vRgns.size();

  ibagCk->setSize((numTotalRgns + 1) * sizeof(sfInstBag));
  ibagCk->data = new uint8_t[ibagCk->size()];

  rgnCounter = 0;
  int instGenCounter = 0;
  int modCounter = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    size_t numRgns = instr->vRgns.size();
    for (size_t j = 0; j < numRgns; j++) {
      sfInstBag instBag{};
      instBag.wInstGenNdx = instGenCounter;
      instGenCounter += numOfGeneratorsForRgn(instr->vRgns[j]);
      instBag.wInstModNdx = modCounter;
      modCounter += instr->vRgns[j]->modulators().size();

      memcpy(ibagCk->data + (rgnCounter++ * sizeof(sfInstBag)), &instBag, sizeof(sfInstBag));
    }
  }
  //  add terminal sfInstBag
  sfInstBag instBag{};
  instBag.wInstGenNdx = instGenCounter;
  instBag.wInstModNdx = modCounter;
  memcpy(ibagCk->data + (rgnCounter * sizeof(sfInstBag)), &instBag, sizeof(sfInstBag));
  pdtaCk->addChildChunk(ibagCk);

  //***********
  // imod chunk
  //***********
  // Calculate total size first
  uint32_t numTotalMods = 0;
  for (const auto instr : synthfile->vInstrs) {
    for (const auto rgn : instr->vRgns) {
      numTotalMods += rgn->modulators().size();
    }
  }

  Chunk *imodCk = new Chunk("imod");
  imodCk->setSize((numTotalMods + 1) * sizeof(sfModList));
  imodCk->data = new uint8_t[imodCk->size()];

  constexpr auto getSf2Src = [](const ModSource &src) -> uint16_t {
    uint16_t res = 0;
    switch (src.type) {
      case ModSourceType::CC:
        res = 0x0080 | src.index;
        break;
      case ModSourceType::PitchBend:
        res = 0x020E;
        break;
      case ModSourceType::NoteOnVelocity:
        res = 0x0002;
        break;
      case ModSourceType::NoteOnKeyNumber:
        res = 0x0003;
        break;
      case ModSourceType::PolyPressure:
        res = 0x000A;
        break;
      case ModSourceType::ChannelPressure:
        res = 0x000D;
        break;
      case ModSourceType::VibratoLFO:
      case ModSourceType::ModulationLFO:
      case ModSourceType::ModulationEnvelope:
      case ModSourceType::VolumeEnvelope:
       // Not direct sources in SF2 (reserved/illegal or no controller)
        res = 0x0000;
        break;
      default:
        break;
    }
    return res | static_cast<uint16_t>(src.flags);
  };

  uint32_t modDataPtr = 0;
  for (const auto instr : synthfile->vInstrs) {
    for (const auto rgn : instr->vRgns) {
      for (const auto &mod : rgn->modulators()) {
        sfInstModList modList{};
        modList.sfModSrcOper = (SFModulator)getSf2Src(mod.source);

        switch (mod.dest) {
          case ModDest::VolAttack:
            modList.sfModDestOper = attackVolEnv;
            break;
          case ModDest::VolHold:
            modList.sfModDestOper = holdVolEnv;
            break;
          case ModDest::VolDecay:
            modList.sfModDestOper = decayVolEnv;
            break;
          case ModDest::VolSustain:
            modList.sfModDestOper = sustainVolEnv;
            break;
          case ModDest::VolRelease:
            modList.sfModDestOper = releaseVolEnv;
            break;
          case ModDest::VolDelay:
            modList.sfModDestOper = delayVolEnv;
            break;
          case ModDest::ModAttack:
            modList.sfModDestOper = attackModEnv;
            break;
          case ModDest::ModHold:
            modList.sfModDestOper = holdModEnv;
            break;
          case ModDest::ModDecay:
            modList.sfModDestOper = decayModEnv;
            break;
          case ModDest::ModSustain:
            modList.sfModDestOper = sustainModEnv;
            break;
          case ModDest::ModRelease:
            modList.sfModDestOper = releaseModEnv;
            break;
          case ModDest::ModDelay:
            modList.sfModDestOper = delayModEnv;
            break;
          case ModDest::Pan:
            modList.sfModDestOper = pan;
            break;
          case ModDest::Attenuation:
            modList.sfModDestOper = initialAttenuation;
            break;
          case ModDest::Pitch:
            modList.sfModDestOper = fineTune;
            break;
          case ModDest::FilterCutoff:
            modList.sfModDestOper = initialFilterFc;
            break;
          case ModDest::FilterQ:
            modList.sfModDestOper = initialFilterQ;
            break;
          case ModDest::ReverbSend:
            modList.sfModDestOper = reverbEffectsSend;
            break;
          case ModDest::ChorusSend:
            modList.sfModDestOper = chorusEffectsSend;
            break;
          case ModDest::VibLfoToPitch:
            modList.sfModDestOper = vibLfoToPitch;
            break;
          case ModDest::VibLfoFreq:
            modList.sfModDestOper = freqVibLFO;
            break;
          case ModDest::VibLfoDelay:
            modList.sfModDestOper = delayVibLFO;
            break;
          case ModDest::ModLfoToPitch:
            modList.sfModDestOper = modLfoToPitch;
            break;
          case ModDest::ModLfoToFilterFc:
            modList.sfModDestOper = modLfoToFilterFc;
            break;
          case ModDest::ModLfoToVolume:
            modList.sfModDestOper = modLfoToVolume;
            break;
          case ModDest::ModLfoFreq:
            modList.sfModDestOper = freqModLFO;
            break;
          case ModDest::ModLfoDelay:
            modList.sfModDestOper = delayModLFO;
            break;
          case ModDest::ModEnvToPitch:
            modList.sfModDestOper = modEnvToPitch;
            break;
          case ModDest::ModEnvToFilterFc:
            modList.sfModDestOper = modEnvToFilterFc;
            break;
          default:
            continue;
          }

        modList.modAmount = mod.amount;
        modList.sfModAmtSrcOper = static_cast<SFModulator>(getSf2Src(mod.sourceAmt));
        modList.sfModTransOper = static_cast<SFTransform>(mod.trans);
        memcpy(imodCk->data + modDataPtr, &modList, sizeof(sfInstModList));
        modDataPtr += sizeof(sfInstModList);
      }
    }
  }

  //  create the terminal field
  sfModList termModList{};
  memcpy(imodCk->data + modDataPtr, &termModList, sizeof(sfModList));
  pdtaCk->addChildChunk(imodCk);

  //***********
  // igen chunk
  //***********
  u32 numTotalGens = 1;
  for (const auto instr : synthfile->vInstrs) {
    for (const auto rgn : instr->vRgns) {
      numTotalGens += numOfGeneratorsForRgn(rgn);
    }
  }

  Chunk *igenCk = new Chunk("igen");
  igenCk->setSize(numTotalGens * sizeof(sfInstGenList));
  igenCk->data = new uint8_t[igenCk->size()];
  dataPtr = 0;
  for (size_t i = 0; i < numInstrs; i++) {
    SynthInstr *instr = synthfile->vInstrs[i];

    size_t numRgns = instr->vRgns.size();
    for (size_t j = 0; j < numRgns; j++) {
      SynthRgn *rgn = instr->vRgns[j];

      sfInstGenList instGenList;
      // Key range. This must be the first chunk
      instGenList.sfGenOper = keyRange;
      instGenList.genAmount.ranges.byLo = static_cast<uint8_t>(rgn->usKeyLow);
      instGenList.genAmount.ranges.byHi = static_cast<uint8_t>(rgn->usKeyHigh);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // Velocity range. This must be the next chunk
      instGenList.sfGenOper = velRange;
      instGenList.genAmount.ranges.byLo = static_cast<uint8_t>(rgn->usVelLow);
      instGenList.genAmount.ranges.byHi = static_cast<uint8_t>(rgn->usVelHigh);
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // initialAttenuation
      instGenList.sfGenOper = initialAttenuation;
      u16 atten = static_cast<u16>(std::clamp(
        std::round((rgn->attenDb + rgn->sampinfo->attenuation) * 10.0),
        0.0, 1440.0));
      instGenList.genAmount.wAmount = atten;
      memcpy(igenCk->data + dataPtr, &instGenList, sizeof(sfInstGenList));
      dataPtr += sizeof(sfInstGenList);

      // pan
      instGenList.sfGenOper = pan;
      instGenList.genAmount.shAmount = static_cast<int16_t>(convertPercentPanTo10thPercentUnits(rgn->art->pan));
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
      instGenList.genAmount.shAmount = static_cast<int16_t>(rgn->art->sustain_lev * 10);
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
      instGenList.genAmount.wAmount = static_cast<uint16_t>(rgn->tableIndex);
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
  shdrCk->setSize(static_cast<uint32_t>((numSamps + 1) * sizeof(sfSample)));
  shdrCk->data = new uint8_t[shdrCk->size()];

  uint32_t sampOffset = 0;
  for (size_t i = 0; i < numSamps; i++) {
    SynthWave *wave = synthfile->vWaves[i];

    sfSample samp{};
    memcpy(samp.achSampleName, wave->name.c_str(), std::min(wave->name.length(), static_cast<size_t>(20)));
    samp.dwStart = sampOffset;
    samp.dwEnd = samp.dwStart + (wave->dataSize / sizeof(uint16_t));
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
    samp.byOriginalKey = static_cast<uint8_t>(sampInfo->usUnityNote) - (sampInfo->sFineTune / 100);
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

std::vector<uint8_t> SF2File::saveToMem() {
  std::vector<uint8_t> buf(size());
  write(buf.data());
  return buf;
}

bool SF2File::saveSF2File(const std::filesystem::path &filepath) {
  auto buf = saveToMem();
  bool result = pRoot->UI_writeBufferToFile(filepath, buf.data(), buf.size());
  return result;
}

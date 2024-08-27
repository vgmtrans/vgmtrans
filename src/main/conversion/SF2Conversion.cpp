/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/
#include "SF2Conversion.h"
#include "SF2File.h"
#include "SynthFile.h"
#include "VGMColl.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "ScaleConversion.h"
#include "LogManager.h"

namespace conversion {

SF2File* createSF2File(const VGMColl& coll) {
  coll.preSynthFileCreation();
  SynthFile *synthfile = createSynthFile(coll.instrSets(), coll.sampColls());
  coll.postSynthFileCreation();
  if (!synthfile) {
    L_ERROR("SF2 conversion for aborted");
    return nullptr;
  }
  SF2File *sf2file = new SF2File(synthfile);
  delete synthfile;
  return sf2file;
}

SF2File* createSF2File(
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls,
  const VGMColl* coll
) {
  if (coll)
    coll->preSynthFileCreation();
  SynthFile *synthfile = createSynthFile(instrsets, sampcolls);
  if (coll)
    coll->postSynthFileCreation();
  if (!synthfile) {
    L_ERROR("SF2 conversion failed");
    return nullptr;
  }
  SF2File *sf2file = new SF2File(synthfile);
  delete synthfile;
  return sf2file;
}

SynthFile* createSynthFile(
  const std::vector<VGMInstrSet*>& m_instrsets,
  const std::vector<VGMSampColl*>& m_sampcolls
) {
  if (m_instrsets.empty()) {
    L_ERROR("No instrument sets available to create a SynthFile.");
    return nullptr;
  }

  /* FIXME: shared_ptr eventually */
  SynthFile *synthfile = new SynthFile("SynthFile");

  std::vector<VGMSamp *> finalSamps;
  std::vector<const VGMSampColl *> finalSampColls;

  /* Grab samples either from the local sampcolls or from the instrument sets */
  if (!m_sampcolls.empty()) {
    for (auto & sampcoll : m_sampcolls) {
      finalSampColls.push_back(sampcoll);
      unpackSampColl(*synthfile, sampcoll, finalSamps);
    }
  } else {
    for (auto & instrset : m_instrsets) {
      if (auto instrset_sampcoll = instrset->sampColl) {
        finalSampColls.push_back(instrset_sampcoll);
        unpackSampColl(*synthfile, instrset_sampcoll, finalSamps);
      }
    }
  }

  if (finalSamps.empty()) {
    L_ERROR("No sample collection available to create a SynthFile.");
    delete synthfile;
    return nullptr;
  }

  for (size_t inst = 0; inst < m_instrsets.size(); inst++) {
    const VGMInstrSet* set = m_instrsets[inst];
    size_t nInstrs = set->aInstrs.size();
    for (size_t i = 0; i < nInstrs; i++) {
      VGMInstr* vgminstr = set->aInstrs[i];
      size_t nRgns = vgminstr->regions().size();
      if (nRgns == 0)  // do not write an instrument if it has no regions
        continue;
      SynthInstr* newInstr = synthfile->addInstr(vgminstr->bank, vgminstr->instrNum, vgminstr->reverb);
      for (uint32_t j = 0; j < nRgns; j++) {
        VGMRgn* rgn = vgminstr->regions()[j];
        //				if (rgn->sampNum+1 > sampColl->samples.size())	//does thereferenced sample exist?
        //					continue;

        // Determine the SampColl associated with this rgn.  If there's an explicit pointer to it, use that.
        const VGMSampColl* sampColl = rgn->sampCollPtr;
        if (!sampColl) {
          // If rgn is of an InstrSet with an embedded SampColl, use that SampColl.
          if (static_cast<VGMInstrSet*>(rgn->vgmFile())->sampColl)
            sampColl = static_cast<VGMInstrSet*>(rgn->vgmFile())->sampColl;
          // If that does not exist, assume the first SampColl
          else
            sampColl = finalSampColls[0];
        }

        // Determine the sample number within the rgn's associated SampColl
        size_t realSampNum;
        // If a sample offset is provided, then find the sample number based on this offset.
        // see sampOffset declaration in header file for more info.
        if (rgn->sampOffset != -1) {
          bool bFoundIt = false;
          for (uint32_t s = 0; s < sampColl->samples.size(); s++) {  //for every sample
            if (rgn->sampOffset == sampColl->samples[s]->dwOffset - sampColl->dwOffset - sampColl->sampDataOffset) {
              realSampNum = s;

              //samples[m]->loop.loopStart = parInstrSet->aInstrs[i]->aRgns[k]->loop.loopStart;
              //samples[m]->loop.loopLength = (samples[m]->dataLength) - (parInstrSet->aInstrs[i]->aRgns[k]->loop.loopStart); //[aInstrs[i]->aRegions[k]->sample_num]->dwUncompSize/2) - ((aInstrs[i]->aRegions[k]->loop_point*28)/16); //to end of sample
              bFoundIt = true;
              break;
            }
          }
          if (!bFoundIt) {
            L_ERROR("Failed matching region to a sample with offset {:#x} (Instrset "
                    "{}, Instr {}, Region {})", rgn->sampOffset, inst, i, j);
            realSampNum = 0;
          }
        }
        // Otherwise, the sample number should be explicitly defined in the rgn.
        else
          realSampNum = rgn->sampNum;

        // Determine the sampCollNum (index into our finalSampColls vector)
        auto sampCollNum = finalSampColls.size();
        for (size_t k = 0; k < finalSampColls.size(); k++) {
          if (finalSampColls[k] == sampColl)
            sampCollNum = k;
        }
        if (sampCollNum == finalSampColls.size()) {
          L_ERROR("SampColl does not exist");
          return nullptr;
        }
        // now we add the number of samples from the preceding SampColls to the value to
        // get the real sampNum in the final DLS file.
        for (uint32_t k = 0; k < sampCollNum; k++)
          realSampNum += finalSampColls[k]->samples.size();

        SynthRgn *newRgn = newInstr->addRgn();
        newRgn->setRanges(rgn->keyLow, rgn->keyHigh, rgn->velLow, rgn->velHigh);
        newRgn->setWaveLinkInfo(0, 0, 1, static_cast<uint32_t>(realSampNum));

        if (realSampNum >= finalSamps.size()) {
          L_ERROR("Sample {} does not exist. Instr index: {:d}, Instr num: {:d}, Region index: {:d}", realSampNum, i, vgminstr->instrNum, j);
          realSampNum = finalSamps.size() - 1;
        }

        VGMSamp* samp = finalSamps[realSampNum];  // sampColl->samples[rgn->sampNum];
        SynthSampInfo* sampInfo = newRgn->addSampInfo();

        // This is a really loopy way of determining the loop information, pardon the pun.  However, it works.
        // There might be a way to simplify this, but I don't want to test out whether another method breaks anything just yet
        // Use the sample's loopStatus to determine if a loop occurs.  If it does, see if the sample provides loop info
        // (gathered during ADPCM > PCM conversion.  If the sample doesn't provide loop offset info, then use the region's
        // loop info.
        if (samp->bPSXLoopInfoPrioritizing) {
          if (samp->loop.loopStatus != -1) {
            if (samp->loop.loopStart != 0 || samp->loop.loopLength != 0)
              sampInfo->setLoopInfo(samp->loop, samp);
            else {
              rgn->loop.loopStatus = samp->loop.loopStatus;
              sampInfo->setLoopInfo(rgn->loop, samp);
            }
          } else {
            delete synthfile;
            throw;
          }
        }
        // The normal method: First, we check if the rgn has loop info defined.
        // If it doesn't, then use the sample's loop info.
        else if (rgn->loop.loopStatus == -1) {
          if (samp->loop.loopStatus != -1)
            sampInfo->setLoopInfo(samp->loop, samp);
          else {
            delete synthfile;
            throw;
          }
        } else
          sampInfo->setLoopInfo(rgn->loop, samp);

        int8_t realUnityKey;
        if (rgn->unityKey == -1)
          realUnityKey = samp->unityKey;
        else
          realUnityKey = rgn->unityKey;
        if (realUnityKey == -1)
          realUnityKey = 0x3C;

        short realFineTune;
        if (rgn->fineTune == 0)
          realFineTune = samp->fineTune;
        else
          realFineTune = rgn->fineTune;

        double attenuation;
        if (rgn->volume != -1)
          attenuation = convertLogScaleValToAtten(rgn->volume);
        else if (samp->volume != -1)
          attenuation = convertLogScaleValToAtten(samp->volume);
        else
          attenuation = 0;

        sampInfo->setPitchInfo(realUnityKey, realFineTune, attenuation);

        double sustainLevAttenDb;
        if (rgn->sustain_level == -1)
          sustainLevAttenDb = 0.0;
        else
          sustainLevAttenDb = convertPercentAmplitudeToAttenDB_SF2(rgn->sustain_level);

        SynthArt *newArt = newRgn->addArt();
        newArt->addPan(rgn->pan);
        newArt->addADSR(rgn->attack_time, static_cast<Transform>(rgn->attack_transform),
          rgn->hold_time, rgn->decay_time, sustainLevAttenDb, rgn->sustain_time, rgn->release_time,
          static_cast<Transform>(rgn->release_transform));
      }
    }
  }
  return synthfile;
}

void unpackSampColl(SynthFile &synthfile, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps) {
  assert(sampColl != nullptr);

  size_t nSamples = sampColl->samples.size();
  for (size_t i = 0; i < nSamples; i++) {
    VGMSamp *samp = sampColl->samples[i];

    uint32_t bufSize;
    if (samp->ulUncompressedSize)
      bufSize = samp->ulUncompressedSize;
    else
      bufSize = static_cast<uint32_t>(ceil(samp->dataLength * samp->compressionRatio()));

    uint8_t *uncompSampBuf = new uint8_t[bufSize];    // create a new memory space for the uncompressed wave
    samp->convertToStdWave(uncompSampBuf);            // and uncompress into that space

    uint16_t blockAlign = samp->bps / 8 * samp->channels;
    SynthWave *wave = synthfile.addWave(1, samp->channels, samp->rate, samp->rate * blockAlign, blockAlign,
                                        samp->bps, bufSize, uncompSampBuf, samp->name());
    finalSamps.push_back(samp);

    // If we don't have any loop information, then don't create a sampInfo structure for the Wave
    if (samp->loop.loopStatus == -1) {
      L_ERROR("No loop information for {} - some parameters might be incorrect", samp->name());
      return;
    }

    SynthSampInfo *sampInfo = wave->addSampInfo();
    if (samp->bPSXLoopInfoPrioritizing) {
      if (samp->loop.loopStart != 0 || samp->loop.loopLength != 0)
        sampInfo->setLoopInfo(samp->loop, samp);
    } else
      sampInfo->setLoopInfo(samp->loop, samp);

    double attenuation = (samp->volume != -1) ? convertLogScaleValToAtten(samp->volume) : 0;
    uint8_t unityKey = (samp->unityKey != -1) ? samp->unityKey : 0x3C;
    short fineTune = samp->fineTune;
    sampInfo->setPitchInfo(unityKey, fineTune, attenuation);
  }
}

} // conversion
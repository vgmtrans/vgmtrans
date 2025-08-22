/*
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
*/
#include <cassert>
#include "DLSConversion.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMColl.h"
#include "DLSFile.h"
#include "Options.h"
#include "ScaleConversion.h"
#include "LogManager.h"

namespace conversion {

void unpackSampColl(DLSFile &dls, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps) {
  assert(sampColl != nullptr);

  size_t nSamples = sampColl->samples.size();
  for (size_t i = 0; i < nSamples; i++) {
    VGMSamp *samp = sampColl->samples[i];

    uint32_t bufSize;
    if (samp->ulUncompressedSize)
      bufSize = samp->ulUncompressedSize;
    else
      bufSize = static_cast<uint32_t>(ceil(samp->dataLength * samp->compressionRatio()));
    auto* uncompSampBuf = new uint8_t[bufSize];    // create a new memory space for the uncompressed wave
    samp->convertToStdWave(uncompSampBuf);            // and uncompress into that space

    uint16_t blockAlign = samp->bps / 8 * samp->channels;
    dls.addWave(1, samp->channels, samp->rate, samp->rate * blockAlign, blockAlign,
                samp->bps, bufSize, uncompSampBuf, samp->name());
    finalSamps.push_back(samp);
  }
}

bool createDLSFile(DLSFile& dls, const VGMColl& coll) {
  bool result = true;
  coll.preSynthFileCreation();
  result &= mainDLSCreation(dls, coll.instrSets(), coll.sampColls());
  coll.postSynthFileCreation();
  return result;
}
bool createDLSFile(
  DLSFile& dls,
  const std::vector<VGMInstrSet*>& instrsets,
  const std::vector<VGMSampColl*>& sampcolls,
  const VGMColl* coll
) {
  bool result = true;
  if (coll)
    coll->preSynthFileCreation();
  result &= mainDLSCreation(dls, instrsets, sampcolls);
  if (coll)
    coll->postSynthFileCreation();
  return result;
}

bool mainDLSCreation(
  DLSFile& dls,
  const std::vector<VGMInstrSet*>& m_instrsets,
  const std::vector<VGMSampColl*>& m_sampcolls
) {
  if (m_instrsets.empty()) {
    L_ERROR("No instrument sets available to create DLS");
    return false;
  }

  std::vector<VGMSamp *> finalSamps;
  std::vector<VGMSampColl *> finalSampColls;

  /* Grab samples either from the local sampcolls or from the instrument sets */
  if (!m_sampcolls.empty()) {
    for (auto & sampcoll : m_sampcolls) {
      finalSampColls.push_back(sampcoll);
      unpackSampColl(dls, sampcoll, finalSamps);
    }
  } else {
    for (auto & instrset : m_instrsets) {
      if (auto instrset_sampcoll = instrset->sampColl) {
        finalSampColls.push_back(instrset_sampcoll);
        unpackSampColl(dls, instrset_sampcoll, finalSamps);
      }
    }
  }

    if (finalSamps.empty()) {
      L_ERROR("No sample collection available to create DLS");
      return false;
    }

  for (size_t inst = 0; inst < m_instrsets.size(); inst++) {
    VGMInstrSet *set = m_instrsets[inst];
    size_t nInstrs = set->aInstrs.size();
    for (size_t i = 0; i < nInstrs; i++) {
      VGMInstr* vgminstr = set->aInstrs[i];
      size_t nRgns = vgminstr->regions().size();
      std::string name = vgminstr->name();
      auto bank_no = vgminstr->bank;
      /*
      * The ulBank field follows this structure:
      * F|00000000000000000|CC0|0|CC32
      * where F = 0 if the instrument is melodic, 1 otherwise
      * (length of each CC is 7 bits, obviously)
      */
      if (auto bs = ConversionOptions::the().bankSelectStyle(); bs == BankSelectStyle::GS) {
        bank_no &= 0x7f;
        bank_no = bank_no << 8;
      } else if (bs == BankSelectStyle::MMA) {
        const uint8_t bank_msb = (bank_no >> 7) & 0x7f;
        const uint8_t bank_lsb = bank_no & 0x7f;
        bank_no = (bank_msb << 8) | bank_lsb;
      }
      DLSInstr *newInstr = dls.addInstr(bank_no, vgminstr->instrNum, name);
      for (uint32_t j = 0; j < nRgns; j++) {
        VGMRgn *rgn = vgminstr->regions()[j];
        //				if (rgn->sampNum+1 > sampColl->samples.size())	//does thereferenced sample exist?
        //					continue;

        // Determine the SampColl associated with this rgn.  If there's an explicit pointer to it, use that.
        VGMSampColl *sampColl = rgn->sampCollPtr;
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
          for (uint32_t s = 0; s < sampColl->samples.size(); s++) {             //for every sample
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
        unsigned int sampCollNum = 0;
        for (unsigned int k = 0; k < finalSampColls.size(); k++) {
          if (finalSampColls[k] == sampColl)
            sampCollNum = k;
        }
        // now we add the number of samples from the preceding SampColls to the value to
        // get the real sampNum in the final DLS file.
        for (unsigned int k = 0; k < sampCollNum; k++)
          realSampNum += finalSampColls[k]->samples.size();

        // For collections with multiple SampColls
        // If a SampColl ptr is given, find the SampColl and adjust the sample number of the region
        // to compensate for all preceding SampColl samples.
        //if (rgn->sampCollNum == -1)	//if a sampCollPtr is defined
        //{
        //	// find the sampColl's index in samplecolls (the sampCollNum, effectively)
        //	for (uint32_t i=0; i < finalSampColls.size(); i++)
        //	{
        //		if (finalSampColls[i] == sampColl)
        //			rgn->sampCollNum = i;
        //	}
        //}
        //if (rgn->sampCollNum != -1)		//if a sampCollNum is defined
        //{									//then sampNum represents the sample number in the specific sample collection
        //	for (int k=0; k < rgn->sampCollNum; k++)
        //		realSampNum += finalSampColls[k]->samples.size();	//so now we add all previous sample collection samples to the value to get the real (absolute) sampNum
        //}

        if (realSampNum >= finalSamps.size()) {
          L_ERROR("Sample {} does not exist", realSampNum);
          realSampNum = finalSamps.size() - 1;
        }

        DLSRgn *newRgn = newInstr->addRgn();
        newRgn->setRanges(rgn->keyLow, rgn->keyHigh, rgn->velLow, rgn->velHigh);
        newRgn->setWaveLinkInfo(0, 0, 1, static_cast<uint32_t>(realSampNum));

        VGMSamp *samp = finalSamps[realSampNum]; //sampColl->samples[rgn->sampNum];
        DLSWsmp *newWsmp = newRgn->addWsmp();

        // This is a really loopy way of determining the loop information, pardon the pun.  However, it works.
        // There might be a way to simplify this, but I don't want to test out whether another method breaks anything just yet
        // Use the sample's loopStatus to determine if a loop occurs.  If it does, see if the sample provides loop info
        // (gathered during ADPCM > PCM conversion.  If the sample doesn't provide loop offset info, then use the region's
        // loop info.
        if (samp->bPSXLoopInfoPrioritizing) {
          if (samp->loop.loopStatus != -1) {
            if (samp->loop.loopStart != 0 || samp->loop.loopLength != 0)
              newWsmp->setLoopInfo(samp->loop, samp);
            else {
              rgn->loop.loopStatus = samp->loop.loopStatus;
              newWsmp->setLoopInfo(rgn->loop, samp);
            }
          } else
              throw;
        }
          // The normal method: First, we check if the rgn has loop info defined.
          // If it doesn't, then use the sample's loop info.
        else if (rgn->loop.loopStatus == -1) {
          if (samp->loop.loopStatus != -1)
            newWsmp->setLoopInfo(samp->loop, samp);
          else
            throw;
        } else
          newWsmp->setLoopInfo(rgn->loop, samp);

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

        long realAttenuation;
        if (rgn->attenDb() == 0 && samp->attenDb() == 0)
          realAttenuation = 0;
        else if (rgn->attenDb() == 0)
          realAttenuation = static_cast<long>(samp->attenDb() * DLS_DECIBEL_UNIT * 10);
        else {
          realAttenuation = static_cast<long>(rgn->attenDb() * DLS_DECIBEL_UNIT * 10);
        }

        long convAttack = static_cast<long>(std::round(secondsToTimecents(rgn->attack_time) * 65536));
        long convHold = static_cast<long>(std::round(secondsToTimecents(rgn->hold_time) * 65536));
        long convDecay = static_cast<long>(std::round(secondsToTimecents(rgn->decay_time) * 65536));
        long convSustainLev;
        if (rgn->sustain_level == -1)
          convSustainLev = 0x03e80000;        //sustain at full if no sustain level provided
        else {
          // the DLS envelope is a range from 0 to -96db.
          double attenInDB = ampToDb(rgn->sustain_level);
          convSustainLev = static_cast<long>(((96.0 - attenInDB) / 96.0) * 0x03e80000);
        }

        long convRelease = static_cast<long>(std::round(secondsToTimecents(rgn->release_time) * 65536));

        DLSArt *newArt = newRgn->addArt();
        newArt->addPan(convertPercentPanTo10thPercentUnits(rgn->pan) * 65536);
        newArt->addADSR(convAttack, 0, convHold, convDecay, convSustainLev, convRelease, 0);

        newWsmp->setPitchInfo(realUnityKey, realFineTune, realAttenuation);
      }
    }
  }
  return true;
}

} // conversion
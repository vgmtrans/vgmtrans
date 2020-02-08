/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMColl.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "ScaleConversion.h"
#include "Root.h"

using namespace std;

DECLARE_MENU(VGMColl)

VGMColl::VGMColl(string theName) : VGMItem(), name(theName), seq(NULL) {}

VGMColl::~VGMColl(void) {}

void VGMColl::RemoveFileAssocs() {
    if (seq)
        seq->RemoveCollAssoc(this);
    for (uint32_t i = 0; i < instrsets.size(); i++)
        instrsets[i]->RemoveCollAssoc(this);
    for (uint32_t i = 0; i < sampcolls.size(); i++)
        sampcolls[i]->RemoveCollAssoc(this);
    for (uint32_t i = 0; i < miscfiles.size(); i++)
        miscfiles[i]->RemoveCollAssoc(this);
}

const string *VGMColl::GetName(void) const {
    return &name;
}

void VGMColl::SetName(const string *newName) {
    name = *newName;
}

VGMSeq *VGMColl::GetSeq(void) {
    return seq;
}

void VGMColl::UseSeq(VGMSeq *theSeq) {
    if (theSeq != NULL)
        theSeq->AddCollAssoc(this);
    if (seq && (theSeq != seq))  // if we associated with a previous sequence
        seq->RemoveCollAssoc(this);
    seq = theSeq;
}

void VGMColl::AddInstrSet(VGMInstrSet *theInstrSet) {
    if (theInstrSet != NULL) {
        theInstrSet->AddCollAssoc(this);
        instrsets.push_back(theInstrSet);
    }
}

void VGMColl::AddSampColl(VGMSampColl *theSampColl) {
    if (theSampColl != NULL) {
        theSampColl->AddCollAssoc(this);
        sampcolls.push_back(theSampColl);
    }
}

void VGMColl::AddMiscFile(VGMFile *theMiscFile) {
    if (theMiscFile != NULL) {
        theMiscFile->AddCollAssoc(this);
        miscfiles.push_back(theMiscFile);
    }
}

bool VGMColl::Load() {
    if (!LoadMain())
        return false;
    pRoot->AddVGMColl(this);
    return true;
}

void VGMColl::UnpackSampColl(DLSFile &dls, VGMSampColl *sampColl, vector<VGMSamp *> &finalSamps) {
    assert(sampColl != NULL);

    size_t nSamples = sampColl->samples.size();
    for (size_t i = 0; i < nSamples; i++) {
        VGMSamp *samp = sampColl->samples[i];

        uint32_t bufSize;
        if (samp->ulUncompressedSize)
            bufSize = samp->ulUncompressedSize;
        else
            bufSize = (uint32_t)ceil((double)samp->dataLength * samp->GetCompressionRatio());
        uint8_t *uncompSampBuf =
            new uint8_t[bufSize];  // create a new memory space for the uncompressed wave
        samp->ConvertToStdWave(uncompSampBuf);  // and uncompress into that space

        uint16_t blockAlign = samp->bps / 8 * samp->channels;
        dls.AddWave(1, samp->channels, samp->rate, samp->rate * blockAlign, blockAlign, samp->bps,
                    bufSize, uncompSampBuf, (samp->name));
        finalSamps.push_back(samp);
    }
}

void VGMColl::UnpackSampColl(SynthFile &synthfile, VGMSampColl *sampColl,
                             vector<VGMSamp *> &finalSamps) {
    assert(sampColl != NULL);

    size_t nSamples = sampColl->samples.size();
    for (size_t i = 0; i < nSamples; i++) {
        VGMSamp *samp = sampColl->samples[i];

        uint32_t bufSize;
        if (samp->ulUncompressedSize)
            bufSize = samp->ulUncompressedSize;
        else
            bufSize = (uint32_t)ceil((double)samp->dataLength * samp->GetCompressionRatio());

        uint8_t *uncompSampBuf =
            new uint8_t[bufSize];  // create a new memory space for the uncompressed wave
        samp->ConvertToStdWave(uncompSampBuf);  // and uncompress into that space

        uint16_t blockAlign = samp->bps / 8 * samp->channels;
        SynthWave *wave =
            synthfile.AddWave(1, samp->channels, samp->rate, samp->rate * blockAlign, blockAlign,
                              samp->bps, bufSize, uncompSampBuf, (samp->name));
        finalSamps.push_back(samp);

        // If we don't have any loop information, then don't create a sampInfo structure for the
        // Wave
        if (samp->loop.loopStatus == -1) {
            L_ERROR("No loop information for {} - some parameters might be incorrect",
                    samp->sampName);
            return;
        }

        SynthSampInfo *sampInfo = wave->AddSampInfo();
        if (samp->bPSXLoopInfoPrioritizing) {
            if (samp->loop.loopStart != 0 || samp->loop.loopLength != 0)
                sampInfo->SetLoopInfo(samp->loop, samp);
        } else
            sampInfo->SetLoopInfo(samp->loop, samp);

        double attenuation = (samp->volume != -1) ? ConvertLogScaleValToAtten(samp->volume) : 0;
        uint8_t unityKey = (samp->unityKey != -1) ? samp->unityKey : 0x3C;
        short fineTune = samp->fineTune;
        sampInfo->SetPitchInfo(unityKey, fineTune, attenuation);
    }
}

bool VGMColl::CreateDLSFile(DLSFile &dls) {
    bool result = true;
    result &= PreDLSMainCreation();
    result &= MainDLSCreation(dls);
    result &= PostDLSMainCreation();
    return result;
}

SF2File *VGMColl::CreateSF2File() {
    SynthFile *synthfile = CreateSynthFile();
    if (!synthfile) {
        L_ERROR("SF2 conversion for '{}' aborted", name);
        return nullptr;
    }

    SF2File *sf2file = new SF2File(synthfile);
    delete synthfile;
    return sf2file;
}

bool VGMColl::MainDLSCreation(DLSFile &dls) {
    if (instrsets.empty()) {
        L_ERROR("{} has no instruments", name);
        return false;
    }

    /* FIXME: shared_ptr eventually */
    SynthFile *synthfile = new SynthFile("SynthFile");

    std::vector<VGMSamp *> finalSamps;
    std::vector<VGMSampColl *> finalSampColls;

    /* Grab samples either from the local sampcolls or from the instrument sets */
    if (!sampcolls.empty()) {
        for (int sam = 0; sam < sampcolls.size(); sam++) {
            finalSampColls.push_back(sampcolls[sam]);
            UnpackSampColl(dls, sampcolls[sam], finalSamps);
        }
    } else {
        for (int i = 0; i < instrsets.size(); i++) {
            auto instrset_sampcoll = instrsets[i]->sampColl;
            if (instrset_sampcoll) {
                finalSampColls.push_back(instrset_sampcoll);
                UnpackSampColl(dls, instrset_sampcoll, finalSamps);
            }
        }
    }

    if (finalSamps.empty()) {
        L_ERROR("No sample collection present for '{}'", name);
        return false;
    }

    for (size_t inst = 0; inst < instrsets.size(); inst++) {
        VGMInstrSet *set = instrsets[inst];
        size_t nInstrs = set->aInstrs.size();
        for (size_t i = 0; i < nInstrs; i++) {
            VGMInstr *vgminstr = set->aInstrs[i];
            size_t nRgns = vgminstr->aRgns.size();
            std::string name = (vgminstr->name);
            DLSInstr *newInstr = dls.AddInstr(vgminstr->bank, vgminstr->instrNum, name);
            for (uint32_t j = 0; j < nRgns; j++) {
                VGMRgn *rgn = vgminstr->aRgns[j];
                //				if (rgn->sampNum+1 > sampColl->samples.size())
                ////does thereferenced sample exist? 					continue;

                // Determine the SampColl associated with this rgn.  If there's an explicit pointer
                // to it, use that.
                VGMSampColl *sampColl = rgn->sampCollPtr;
                if (!sampColl) {
                    // If rgn is of an InstrSet with an embedded SampColl, use that SampColl.
                    if (((VGMInstrSet *)rgn->vgmfile)->sampColl)
                        sampColl = ((VGMInstrSet *)rgn->vgmfile)->sampColl;

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
                    for (uint32_t s = 0; s < sampColl->samples.size(); s++) {  // for every sample
                        if (rgn->sampOffset == sampColl->samples[s]->dwOffset - sampColl->dwOffset -
                                                   sampColl->sampDataOffset) {
                            realSampNum = s;

                            // samples[m]->loop.loopStart =
                            // parInstrSet->aInstrs[i]->aRgns[k]->loop.loopStart;
                            // samples[m]->loop.loopLength = (samples[m]->dataLength) -
                            // (parInstrSet->aInstrs[i]->aRgns[k]->loop.loopStart);
                            // //[aInstrs[i]->aRegions[k]->sample_num]->dwUncompSize/2) -
                            // ((aInstrs[i]->aRegions[k]->loop_point*28)/16); //to end of sample
                            bFoundIt = true;
                            break;
                        }
                    }
                    if (!bFoundIt) {
                        L_ERROR("Failed matching region to a sample with offset {:#x} (Instrset "
                                "{}, Instr {}, Region {})",
                                rgn->sampOffset, inst, i, j);
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
                //   now we add the number of samples from the preceding SampColls to the value to
                //   get the real sampNum in the final DLS file.
                for (unsigned int k = 0; k < sampCollNum; k++)
                    realSampNum += finalSampColls[k]->samples.size();

                // For collections with multiple SampColls
                // If a SampColl ptr is given, find the SampColl and adjust the sample number of the
                // region to compensate for all preceding SampColl samples.
                // if (rgn->sampCollNum == -1)	//if a sampCollPtr is defined
                //{
                //	// find the sampColl's index in samplecolls (the sampCollNum, effectively)
                //	for (uint32_t i=0; i < finalSampColls.size(); i++)
                //	{
                //		if (finalSampColls[i] == sampColl)
                //			rgn->sampCollNum = i;
                //	}
                //}
                // if (rgn->sampCollNum != -1)		//if a sampCollNum is defined
                //{									//then
                // sampNum represents
                // the sample number in the specific sample collection 	for (int k=0; k <
                // rgn->sampCollNum; k++) 		realSampNum +=
                // finalSampColls[k]->samples.size();
                ////so now we add all previous sample collection samples to the value to get the
                // real (absolute) sampNum
                //}

                DLSRgn *newRgn = newInstr->AddRgn();
                newRgn->SetRanges(rgn->keyLow, rgn->keyHigh, rgn->velLow, rgn->velHigh);
                newRgn->SetWaveLinkInfo(0, 0, 1, (uint32_t)realSampNum);

                if (realSampNum >= finalSamps.size()) {
                    L_ERROR("Sample {} does not exist", realSampNum);
                    realSampNum = finalSamps.size() - 1;
                }

                VGMSamp *samp = finalSamps[realSampNum];  // sampColl->samples[rgn->sampNum];
                DLSWsmp *newWsmp = newRgn->AddWsmp();

                // This is a really loopy way of determining the loop information, pardon the pun.
                // However, it works. There might be a way to simplify this, but I don't want to
                // test out whether another method breaks anything just yet Use the sample's
                // loopStatus to determine if a loop occurs.  If it does, see if the sample provides
                // loop info (gathered during ADPCM > PCM conversion.  If the sample doesn't provide
                // loop offset info, then use the region's loop info.
                if (samp->bPSXLoopInfoPrioritizing) {
                    if (samp->loop.loopStatus != -1) {
                        if (samp->loop.loopStart != 0 || samp->loop.loopLength != 0)
                            newWsmp->SetLoopInfo(samp->loop, samp);
                        else {
                            rgn->loop.loopStatus = samp->loop.loopStatus;
                            newWsmp->SetLoopInfo(rgn->loop, samp);
                        }
                    } else
                        throw;
                }
                // The normal method: First, we check if the rgn has loop info defined.
                // If it doesn't, then use the sample's loop info.
                else if (rgn->loop.loopStatus == -1) {
                    if (samp->loop.loopStatus != -1)
                        newWsmp->SetLoopInfo(samp->loop, samp);
                    else
                        throw;
                } else
                    newWsmp->SetLoopInfo(rgn->loop, samp);

                int8_t realUnityKey = -1;
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
                if (rgn->volume == -1 && samp->volume == -1)
                    realAttenuation = 0;
                else if (rgn->volume == -1)
                    realAttenuation =
                        (long)(-(ConvertLogScaleValToAtten(samp->volume) * DLS_DECIBEL_UNIT * 10));
                else
                    realAttenuation =
                        (long)(-(ConvertLogScaleValToAtten(rgn->volume) * DLS_DECIBEL_UNIT * 10));

                long convAttack = (long)std::round(SecondsToTimecents(rgn->attack_time) * 65536);
                long convDecay = (long)std::round(SecondsToTimecents(rgn->decay_time) * 65536);
                long convSustainLev;
                if (rgn->sustain_level == -1)
                    convSustainLev = 0x03e80000;  // sustain at full if no sustain level provided
                else {
                    // the DLS envelope is a range from 0 to -96db.
                    double attenInDB = ConvertLogScaleValToAtten(rgn->sustain_level);
                    convSustainLev = (long)(((96.0 - attenInDB) / 96.0) * 0x03e80000);
                }

                long convRelease = (long)std::round(SecondsToTimecents(rgn->release_time) * 65536);

                DLSArt *newArt = newRgn->AddArt();
                newArt->AddPan(ConvertPercentPanTo10thPercentUnits(rgn->pan) * 65536);
                newArt->AddADSR(convAttack, 0, convDecay, convSustainLev, convRelease, 0);

                newWsmp->SetPitchInfo(realUnityKey, realFineTune, realAttenuation);
            }
        }
    }
    return true;
}

SynthFile *VGMColl::CreateSynthFile() {
    if (instrsets.empty()) {
        L_ERROR("{} has no instruments", name);
        return nullptr;
    }

    /* FIXME: shared_ptr eventually */
    SynthFile *synthfile = new SynthFile("SynthFile");

    std::vector<VGMSamp *> finalSamps;
    std::vector<VGMSampColl *> finalSampColls;

    /* Grab samples either from the local sampcolls or from the instrument sets */
    if (!sampcolls.empty()) {
        for (int sam = 0; sam < sampcolls.size(); sam++) {
            finalSampColls.push_back(sampcolls[sam]);
            UnpackSampColl(*synthfile, sampcolls[sam], finalSamps);
        }
    } else {
        for (int i = 0; i < instrsets.size(); i++) {
            auto instrset_sampcoll = instrsets[i]->sampColl;
            if (instrset_sampcoll) {
                finalSampColls.push_back(instrset_sampcoll);
                UnpackSampColl(*synthfile, instrset_sampcoll, finalSamps);
            }
        }
    }

    if (finalSamps.empty()) {
        L_ERROR("No sample collection present for '{}'", name);
        delete synthfile;
        return nullptr;
    }

    for (size_t inst = 0; inst < instrsets.size(); inst++) {
        VGMInstrSet *set = instrsets[inst];
        size_t nInstrs = set->aInstrs.size();
        for (size_t i = 0; i < nInstrs; i++) {
            VGMInstr *vgminstr = set->aInstrs[i];
            size_t nRgns = vgminstr->aRgns.size();
            if (nRgns == 0)  // do not write an instrument if it has no regions
                continue;
            SynthInstr *newInstr = synthfile->AddInstr(vgminstr->bank, vgminstr->instrNum);
            for (uint32_t j = 0; j < nRgns; j++) {
                VGMRgn *rgn = vgminstr->aRgns[j];
                //				if (rgn->sampNum+1 > sampColl->samples.size())
                ////does thereferenced sample exist? 					continue;

                // Determine the SampColl associated with this rgn.  If there's an explicit pointer
                // to it, use that.
                VGMSampColl *sampColl = rgn->sampCollPtr;
                if (!sampColl) {
                    // If rgn is of an InstrSet with an embedded SampColl, use that SampColl.
                    if (((VGMInstrSet *)rgn->vgmfile)->sampColl)
                        sampColl = ((VGMInstrSet *)rgn->vgmfile)->sampColl;

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
                    for (uint32_t s = 0; s < sampColl->samples.size(); s++) {  // for every sample
                        if (rgn->sampOffset == sampColl->samples[s]->dwOffset - sampColl->dwOffset -
                                                   sampColl->sampDataOffset) {
                            realSampNum = s;

                            // samples[m]->loop.loopStart =
                            // parInstrSet->aInstrs[i]->aRgns[k]->loop.loopStart;
                            // samples[m]->loop.loopLength = (samples[m]->dataLength) -
                            // (parInstrSet->aInstrs[i]->aRgns[k]->loop.loopStart);
                            // //[aInstrs[i]->aRegions[k]->sample_num]->dwUncompSize/2) -
                            // ((aInstrs[i]->aRegions[k]->loop_point*28)/16); //to end of sample
                            bFoundIt = true;
                            break;
                        }
                    }
                    if (!bFoundIt) {
                        L_ERROR("Failed matching region to a sample with offset {:#x} (Instrset "
                                "{}, Instr {}, Region {})",
                                rgn->sampOffset, inst, i, j);
                        realSampNum = 0;
                    }
                }
                // Otherwise, the sample number should be explicitly defined in the rgn.
                else
                    realSampNum = rgn->sampNum;

                // Determine the sampCollNum (index into our finalSampColls vector)
                auto sampCollNum = finalSampColls.size();
                for (size_t i = 0; i < finalSampColls.size(); i++) {
                    if (finalSampColls[i] == sampColl)
                        sampCollNum = i;
                }
                if (sampCollNum == finalSampColls.size()) {
                    L_ERROR("SampColl does not exist");
                    return NULL;
                }
                //   now we add the number of samples from the preceding SampColls to the value to
                //   get the real sampNum in the final DLS file.
                for (uint32_t k = 0; k < sampCollNum; k++)
                    realSampNum += finalSampColls[k]->samples.size();

                SynthRgn *newRgn = newInstr->AddRgn();
                newRgn->SetRanges(rgn->keyLow, rgn->keyHigh, rgn->velLow, rgn->velHigh);
                newRgn->SetWaveLinkInfo(0, 0, 1, (uint32_t)realSampNum);

                if (realSampNum >= finalSamps.size()) {
                    L_ERROR("Sample {} does not exist", realSampNum);
                    realSampNum = finalSamps.size() - 1;
                }

                VGMSamp *samp = finalSamps[realSampNum];  // sampColl->samples[rgn->sampNum];
                SynthSampInfo *sampInfo = newRgn->AddSampInfo();

                // This is a really loopy way of determining the loop information, pardon the pun.
                // However, it works. There might be a way to simplify this, but I don't want to
                // test out whether another method breaks anything just yet Use the sample's
                // loopStatus to determine if a loop occurs.  If it does, see if the sample provides
                // loop info (gathered during ADPCM > PCM conversion.  If the sample doesn't provide
                // loop offset info, then use the region's loop info.
                if (samp->bPSXLoopInfoPrioritizing) {
                    if (samp->loop.loopStatus != -1) {
                        if (samp->loop.loopStart != 0 || samp->loop.loopLength != 0)
                            sampInfo->SetLoopInfo(samp->loop, samp);
                        else {
                            rgn->loop.loopStatus = samp->loop.loopStatus;
                            sampInfo->SetLoopInfo(rgn->loop, samp);
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
                        sampInfo->SetLoopInfo(samp->loop, samp);
                    else {
                        delete synthfile;
                        throw;
                    }
                } else
                    sampInfo->SetLoopInfo(rgn->loop, samp);

                int8_t realUnityKey = -1;
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
                    attenuation = ConvertLogScaleValToAtten(rgn->volume);
                else if (samp->volume != -1)
                    attenuation = ConvertLogScaleValToAtten(samp->volume);
                else
                    attenuation = 0;

                double sustainLevAttenDb;
                if (rgn->sustain_level == -1)
                    sustainLevAttenDb = 0.0;
                else
                    sustainLevAttenDb = ConvertPercentAmplitudeToAttenDB_SF2(rgn->sustain_level);

                SynthArt *newArt = newRgn->AddArt();
                newArt->AddPan(rgn->pan);
                newArt->AddADSR(rgn->attack_time, (Transform)rgn->attack_transform, rgn->decay_time,
                                sustainLevAttenDb, rgn->sustain_time, rgn->release_time,
                                (Transform)rgn->release_transform);

                sampInfo->SetPitchInfo(realUnityKey, realFineTune, attenuation);
            }
        }
    }
    return synthfile;
}

bool VGMColl::OnSaveAll() {
    if (dirpath.empty()) {
        dirpath = pRoot->UI_GetSaveDirPath();
    }

    SF2File *sf2file = CreateSF2File();
    auto filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".sf2";
    if (sf2file != nullptr) {
        if (!sf2file->SaveSF2File(filepath))
            L_ERROR("Failed to save SF2 file");
        delete sf2file;
    } else {
        L_ERROR("Failed creating SF2");
    }

    DLSFile dlsfile;
    filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".dls";
    if (CreateDLSFile(dlsfile)) {
        if (!dlsfile.SaveDLSFile(filepath))
            L_ERROR("Failed to save DLS file");
    } else {
        L_ERROR("Failed creating DLS instance");
    }

    if (this->seq != nullptr) {
        filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".mid";
        if (!this->seq->SaveAsMidi(filepath)) {
            L_ERROR("Failed to save MIDI file");
        }
    }

    dirpath.clear();

    return true;
}

bool VGMColl::OnSaveAllDLS() {
    if (dirpath.empty()) {
        dirpath = pRoot->UI_GetSaveDirPath();
    }

    SF2File *sf2file = CreateSF2File();
    auto filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".sf2";
    if (sf2file != nullptr) {
        if (!sf2file->SaveSF2File(filepath))
            L_ERROR("Failed to save SF2 file");
        delete sf2file;
    } else {
        L_ERROR("Failed creating SF2");
    }

    DLSFile dlsfile;
    filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".dls";
    if (CreateDLSFile(dlsfile)) {
        if (!dlsfile.SaveDLSFile(filepath))
            L_ERROR("Failed to save DLS file");
    } else {
        L_ERROR("Failed creating DLS instance");
    }

    if (this->seq != nullptr) {
        filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".mid";
        if (!this->seq->SaveAsMidi(filepath))
            L_ERROR("Failed to save MIDI file");
    }

    dirpath.clear();

    return true;
}

bool VGMColl::OnSaveAllSF2() {
    if (dirpath.empty()) {
        dirpath = pRoot->UI_GetSaveDirPath();
    }

    auto filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".sf2";
    SF2File *sf2file = CreateSF2File();
    if (sf2file != nullptr) {
        if (!sf2file->SaveSF2File(filepath))
            L_ERROR("Failed to save SF2 file");
        delete sf2file;
    } else {
        L_ERROR("Failed creating SF2");
    }

    if (this->seq != nullptr) {
        filepath = dirpath + "/" + ConvertToSafeFileName(this->name) + ".mid";
        if (!this->seq->SaveAsMidi(filepath))
            L_ERROR("Failed to save MIDI file");
    }

    dirpath.clear();

    return true;
}

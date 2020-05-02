/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include "VGMFile.h"

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;
class VGMSamp;
class DLSFile;
class SF2File;
class SynthFile;

class VGMColl : public VGMItem {
   public:
    explicit VGMColl(std::string name = "Unnamed Collection");
    virtual ~VGMColl() = default;

    void RemoveFileAssocs();
    [[nodiscard]] const std::string &GetName() const;
    void SetName(const std::string *newName);
    VGMSeq *GetSeq();
    void UseSeq(VGMSeq *theSeq);
    void AddInstrSet(VGMInstrSet *theInstrSet);
    void AddSampColl(VGMSampColl *theSampColl);
    void AddMiscFile(VGMMiscFile *theMiscFile);
    bool Load();
    virtual bool LoadMain() { return true; }
    virtual bool CreateDLSFile(DLSFile &dls);
    virtual SF2File *CreateSF2File();
    virtual bool PreDLSMainCreation() { return true; }
    virtual SynthFile *CreateSynthFile();
    virtual bool MainDLSCreation(DLSFile &dls);
    virtual bool PostDLSMainCreation() { return true; }

    VGMSeq *seq;
    std::vector<VGMInstrSet *> instrsets;
    std::vector<VGMSampColl *> sampcolls;
    std::vector<VGMMiscFile *> miscfiles;

   protected:
    void UnpackSampColl(DLSFile &dls, VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);
    void UnpackSampColl(SynthFile &synthfile, VGMSampColl *sampColl,
                        std::vector<VGMSamp *> &finalSamps);
    std::string name;
};

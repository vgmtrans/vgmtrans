/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include "Menu.h"

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMSamp;
class DLSFile;
class SF2File;
class SynthFile;

class VGMColl : public VGMItem {
   public:
    BEGIN_MENU(VGMColl)
    MENU_ITEM(VGMColl, OnSaveAllDLS, "Save as MIDI and DLS.")
    MENU_ITEM(VGMColl, OnSaveAllSF2, "Save as MIDI and SoundFont 2.")
    MENU_ITEM(VGMColl, OnSaveAll, "Save as MIDI, SoundFont 2 and DLS")
    END_MENU()

    explicit VGMColl(std::string name = "Unnamed Collection");
    virtual ~VGMColl() = default;

    void RemoveFileAssocs();
    [[nodiscard]] const std::string &GetName() const;
    void SetName(const std::string *newName);
    VGMSeq *GetSeq();
    void UseSeq(VGMSeq *theSeq);
    void AddInstrSet(VGMInstrSet *theInstrSet);
    void AddSampColl(VGMSampColl *theSampColl);
    void AddMiscFile(VGMFile *theMiscFile);
    bool Load();
    virtual bool LoadMain() { return true; }
    virtual bool CreateDLSFile(DLSFile &dls);
    virtual SF2File *CreateSF2File();
    virtual bool PreDLSMainCreation() { return true; }
    virtual SynthFile *CreateSynthFile();
    virtual bool MainDLSCreation(DLSFile &dls);
    virtual bool PostDLSMainCreation() { return true; }

    // This feels stupid, but the current callbacks system
    // is not exactly flexible.
    inline void SetDefaultSavePath(std::string savepath) { dirpath = std::move(savepath); }

    bool OnSaveAll();
    bool OnSaveAllDLS();
    bool OnSaveAllSF2();

    VGMSeq *seq;
    std::vector<VGMInstrSet *> instrsets;
    std::vector<VGMSampColl *> sampcolls;
    std::vector<VGMFile *> miscfiles;

   protected:
    void UnpackSampColl(DLSFile &dls, VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);
    void UnpackSampColl(SynthFile &synthfile, VGMSampColl *sampColl,
                        std::vector<VGMSamp *> &finalSamps);

   protected:
    std::string name;
    std::string dirpath = "";
};

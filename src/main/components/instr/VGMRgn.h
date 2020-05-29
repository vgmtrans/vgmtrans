/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "SynthFile.h"
#include "VGMItem.h"
#include "Loop.h"

class VGMInstr;
class VGMRgnItem;
class VGMSampColl;

// ******
// VGMRgn
// ******

class VGMRgn : public VGMContainerItem {
   public:
    VGMRgn(VGMInstr *instr, uint32_t offset, uint32_t length = 0, std::string name = "Region");
    VGMRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t keyLow, uint8_t keyHigh,
           uint8_t velLow, uint8_t velHigh, int sampNum, std::string name = "Region");
    ~VGMRgn();

    virtual bool LoadRgn() { return true; }

    void SetRanges(uint8_t keyLow, uint8_t keyHigh, uint8_t velLow = 0, uint8_t velHigh = 0x7F);
    void SetUnityKey(int8_t unityNote);
    void SetSampNum(size_t sampNumber);
    void SetLoopInfo(int theLoopStatus, uint32_t theLoopStart, uint32_t theLoopLength);
    void SetADSR(long attack_time, uint16_t atk_transform, long decay_time, long sustain_lev,
                 uint16_t rls_transform, long release_time);

    void AddGeneralItem(uint32_t offset, uint32_t length, const std::string &name);
    void AddUnknown(uint32_t offset, uint32_t length);
    void SetFineTune(int16_t relativePitchCents) { fineTune = relativePitchCents; }
    void SetPan(uint8_t pan);
    void AddPan(uint8_t pan, uint32_t offset, uint32_t length = 1);
    void SetVolume(double volume);
    void AddVolume(double volume, uint32_t offset, uint32_t length = 1);
    void AddUnityKey(int8_t unityKey, uint32_t offset, uint32_t length = 1);
    void AddFineTune(int16_t relativePitchCents, uint32_t offset, uint32_t length = 1);
    void AddKeyLow(uint8_t keyLow, uint32_t offset, uint32_t length = 1);
    void AddKeyHigh(uint8_t keyHigh, uint32_t offset, uint32_t length = 1);
    void AddVelLow(uint8_t velLow, uint32_t offset, uint32_t length = 1);
    void AddVelHigh(uint8_t velHigh, uint32_t offset, uint32_t length = 1);
    void AddSampNum(int sampNum, uint32_t offset, uint32_t length = 1);

    VGMInstr *parInstr = nullptr;
    uint8_t keyLow = 0;
    uint8_t keyHigh = 127;
    uint8_t velLow = 0;
    uint8_t velHigh = 127;

    int8_t unityKey = -1;
    short fineTune = 0;

    Loop loop;

    int sampNum = 0;
    uint32_t sampOffset = -1; /* Offset wrt whatever collection of samples we have */
    VGMSampColl *sampCollPtr = nullptr;

    double volume = -1;        /* Percentage of full volume */
    double pan = 0.5;          /* Left 0 <- 0.5 Center -> 1 Right */
    double attack_time = 0;    /* In seconds */
    double decay_time = 0;     /* In seconds */
    double release_time = 0;   /* In seconds */
    double sustain_level = -1; /* Percentage */
    double sustain_time = 0;   /* In seconds (no positive rate!) */

    uint16_t attack_transform = no_transform;
    uint16_t release_transform = no_transform;

    std::vector<VGMRgnItem *> items;
};

// **********
// VGMRgnItem
// **********

class VGMRgnItem : public VGMItem {
   public:
    enum RgnItemType {
        RIT_GENERIC,
        RIT_UNKNOWN,
        RIT_UNITYKEY,
        RIT_FINETUNE,
        RIT_KEYLOW,
        RIT_KEYHIGH,
        RIT_VELLOW,
        RIT_VELHIGH,
        RIT_PAN,
        RIT_VOL,
        RIT_SAMPNUM
    };

    VGMRgnItem(VGMRgn *rgn, RgnItemType theType, uint32_t offset, uint32_t length,
               const std::string &name);
    virtual Icon GetIcon();

   public:
    RgnItemType type;
};

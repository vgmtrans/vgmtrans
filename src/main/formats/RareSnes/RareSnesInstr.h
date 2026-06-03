#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <map>
#include <string>
#include <vector>

// ****************
// RareSnesInstrSet
// ****************

class RareSnesInstrSet:
    public VGMInstrSet {
 public:
  RareSnesInstrSet(RawFile *file, u32 offset, u32 spcDirAddress, const std::string &name = "RareSnesInstrSet");
  RareSnesInstrSet(RawFile *file,
                   u32 offset,
                   u32 spcDirAddress,
                   const std::map<u8, s8> &unityKeyHints,
                   const std::map<u8, s16> &pitchHints,
                   const std::map<u8, u16> &adsrHints,
                   const std::string &name = "RareSnesInstrSet");
  virtual ~RareSnesInstrSet();

  virtual void Initialize();
  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  const std::vector<u8> &getAvailableInstruments();

 protected:
  u32 spcDirAddr;
  u8 maxSRCNValue;
  std::vector<u8> availInstruments;
  std::map<u8, s8> instrUnityKeyHints;
  std::map<u8, s16> instrPitchHints;
  std::map<u8, u16> instrADSRHints;

  void ScanAvailableInstruments();
};

// *************
// RareSnesInstr
// *************

class RareSnesInstr
    : public VGMInstr {
 public:
  RareSnesInstr(VGMInstrSet *instrSet,
                u32 offset,
                u32 theBank,
                u32 theInstrNum,
                u32 spcDirAddress,
                s8 transposeSemitones = 0,
                s16 pitchValue = 0,
                u16 adsrValue = 0x8FE0,
                const std::string &name = "RareSnesInstr");
  virtual ~RareSnesInstr();

  virtual bool loadInstr();

 protected:
  u32 spcDirAddr;
  s8 transpose;
  s16 pitch;
  u16 adsr;
};

// ***********
// RareSnesRgn
// ***********

class RareSnesRgn
    : public VGMRgn {
 public:
  RareSnesRgn(RareSnesInstr *instr, u32 offset, s8 transposeSemitones = 0,
              s16 pitchValue = 0, u16 adsrValue = 0x8FE0);
  virtual ~RareSnesRgn();

  virtual bool loadRgn();

 protected:
  s8 transpose;
  s16 pitch;
  u16 adsr;
};

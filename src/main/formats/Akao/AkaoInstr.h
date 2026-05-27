/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include <set>
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "AkaoFormat.h"

struct AkaoInstrDatLocation {
  u32 instrAllOffset;
  u32 instrDatOffset;
  u32 startingArticulationId;
  u32 numArticulations;

  AkaoInstrDatLocation() noexcept : instrAllOffset(0), instrDatOffset(0), startingArticulationId(0), numArticulations(0) {}

  AkaoInstrDatLocation(u32 instrAllOffset, u32 instrDatOffset, u32 startingArticulationId, u32 numArticulations) noexcept
    : instrAllOffset(instrAllOffset), instrDatOffset(instrDatOffset), startingArticulationId(startingArticulationId), numArticulations(numArticulations) {}
};

// ************
// AkaoInstrSet
// ************

class AkaoInstrSet final : public VGMInstrSet {
 public:
  AkaoInstrSet(RawFile *file,
               u32 length,
               AkaoPs1Version version,
               u32 instrOff,
               u32 dkitOff,
               u32 id,
               std::string name = "Akao Instrument Bank");
  AkaoInstrSet(RawFile *file, u32 end_boundary_offset, AkaoPs1Version version,
    const std::set<u32>& custom_instrument_addresses, const std::set<u32>& drum_instrument_addresses,
    u32 id, std::string name = "Akao Instrument Bank");
  AkaoInstrSet(RawFile *file, u32 offset, u32 end_boundary_offset, AkaoPs1Version version,
    u32 id, std::string name = "Akao Instrument Bank (Dummy)");
  bool parseInstrPointers() override;
  void useColl(const VGMColl* coll) override;

  [[nodiscard]] AkaoPs1Version version() const noexcept { return version_; }

  bool bMelInstrs, bDrumKit;
  u32 instrSetOff;
  u32 drumkitOff;
  u32 end_boundary_offset;
  std::set<u32> custom_instrument_addresses;
  std::set<u32> drum_instrument_addresses;

 private:
  AkaoPs1Version version_;
};

// *********
// AkaoInstr
// *********


class AkaoInstr: public VGMInstr {
 public:
  AkaoInstr(AkaoInstrSet *instrSet,
            u32 offset,
            u32 length,
            u32 bank,
            u32 instrNum,
            std::string name = "Instrument");
  bool loadInstr() override;

  [[nodiscard]] AkaoInstrSet * instrSet() const noexcept { return reinterpret_cast<AkaoInstrSet*>(this->parInstrSet); }

  [[nodiscard]] AkaoPs1Version version() const noexcept { return instrSet()->version(); }

  bool bDrumKit;
};

// ***********
// AkaoDrumKit
// ***********


class AkaoDrumKit final : public AkaoInstr {
 public:
  AkaoDrumKit(AkaoInstrSet *instrSet, u32 offset, u32 length, u32 bank, u32 instrNum);
  bool loadInstr() override;
};


// *******
// AkaoRgn
// *******

class AkaoRgn final :
    public VGMRgn {
 public:
  AkaoRgn(VGMInstr *instr, u32 offset, u32 length = 0, std::string name = "Region");
  AkaoRgn(VGMInstr *instr, u32 offset, u32 length, u8 keyLow, u8 keyHigh,
          u8 artIDNum, std::string name = "Region");

  bool loadRgn() override;

  unsigned short adsr1;  //raw psx ADSR1 value (articulation data)
  unsigned short adsr2;  //raw psx ADSR2 value (articulation data)
  u8 artNum;
  u8 drumRelUnityKey;
  u8 attackRate;
  u8 sustainRate;
  u8 sustainMode;
  u8 releaseRate;
};


// ***********
// AkaoSampColl
// ***********

struct AkaoArt {
  u8 unityKey;
  short fineTune;
  u32 sample_offset;
  u32 loop_point;
  u16 ADSR1;
  u16 ADSR2;
  u32 artID;
  int sample_num;

  AkaoArt() : unityKey(0), fineTune(0), sample_offset(0), loop_point(0), ADSR1(0), ADSR2(0), artID(0), sample_num(0){}
};


class AkaoSampColl final :
    public VGMSampColl {
 public:
   AkaoSampColl(RawFile *file, u32 offset, AkaoPs1Version version, std::string name = "Akao Sample Collection");
   AkaoSampColl(RawFile *file, AkaoInstrDatLocation file_location, std::string name = "Akao Sample Collection");

  bool parseHeader() override;
  bool parseSampleInfo() override;

  [[nodiscard]] AkaoPs1Version version() const noexcept { return version_; }

  [[nodiscard]] static bool isPossibleAkaoSampColl(const RawFile *file, u32 offset);
  [[nodiscard]] static AkaoPs1Version guessVersion(const RawFile *file, u32 offset);

  std::vector<AkaoArt> akArts;
  u32 starting_art_id;
  u32 ending_art_id;
  u32 nNumArts;
  u16 sample_set_id;

 private:
  AkaoPs1Version version_;
  u32 sample_section_size;
  u32 arts_offset;
  u32 sample_section_offset;
  AkaoInstrDatLocation file_location;
};

#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "AkaoFormat.h"
#include "Matcher.h"

struct AkaoInstrDatLocation {
  uint32_t instrAllOffset;
  uint32_t instrDatOffset;
  uint32_t startingArticulationId;
  uint32_t numArticulations;

  AkaoInstrDatLocation() noexcept : instrAllOffset(0), instrDatOffset(0), startingArticulationId(0), numArticulations(0) {}

  AkaoInstrDatLocation(uint32_t instrAllOffset, uint32_t instrDatOffset, uint32_t startingArticulationId, uint32_t numArticulations) noexcept
    : instrAllOffset(instrAllOffset), instrDatOffset(instrDatOffset), startingArticulationId(startingArticulationId), numArticulations(numArticulations) {}
};

// ************
// AkaoInstrSet
// ************

class AkaoInstrSet final : public VGMInstrSet {
 public:
  AkaoInstrSet(RawFile *file,
               uint32_t length,
               AkaoPs1Version version,
               uint32_t instrOff,
               uint32_t dkitOff,
               uint32_t id,
               std::wstring name = L"Akao Instrument Bank");
  AkaoInstrSet(RawFile *file, uint32_t end_boundary_offset, AkaoPs1Version version, std::set<uint32_t> custom_instrument_addresses, std::set<uint32_t> drum_instrument_addresses, std::wstring name = L"Akao Instrument Bank");
  AkaoInstrSet(RawFile *file, uint32_t offset, uint32_t end_boundary_offset, AkaoPs1Version version, std::wstring name = L"Akao Instrument Bank (Dummy)");
  bool GetInstrPointers() override;

  [[nodiscard]] AkaoPs1Version version() const noexcept { return version_; }

  bool bMelInstrs, bDrumKit;
  uint32_t instrSetOff;
  uint32_t drumkitOff;
  uint32_t end_boundary_offset;
  std::set<uint32_t> custom_instrument_addresses;
  std::set<uint32_t> drum_instrument_addresses;

 private:
  AkaoPs1Version version_;
};

// *********
// AkaoInstr
// *********


class AkaoInstr: public VGMInstr {
 public:
  AkaoInstr(AkaoInstrSet *instrSet,
            uint32_t offset,
            uint32_t length,
            uint32_t bank,
            uint32_t instrNum,
            std::wstring name = L"Instrument");
  bool LoadInstr() override;

  [[nodiscard]] AkaoInstrSet * instrSet() const noexcept { return reinterpret_cast<AkaoInstrSet*>(this->parInstrSet); }

  [[nodiscard]] AkaoPs1Version version() const noexcept { return instrSet()->version(); }

  bool bDrumKit;
};

// ***********
// AkaoDrumKit
// ***********


class AkaoDrumKit final : public AkaoInstr {
 public:
  AkaoDrumKit(AkaoInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t bank, uint32_t instrNum);
  bool LoadInstr() override;
};


// *******
// AkaoRgn
// *******

class AkaoRgn final :
    public VGMRgn {
 public:
  AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length = 0, std::wstring name = L"Region");
  AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t keyLow, uint8_t keyHigh,
          uint8_t artIDNum, std::wstring name = L"Region");

  bool LoadRgn() override;

  unsigned short adsr1;                //raw psx ADSR1 value (articulation data)
  unsigned short adsr2;                //raw psx ADSR2 value (articulation data)
  uint8_t artNum;
  uint8_t drumRelUnityKey;
};


// ***********
// AkaoSampColl
// ***********

struct AkaoArt {
  uint8_t unityKey;
  short fineTune;
  uint32_t sample_offset;
  uint32_t loop_point;
  uint16_t ADSR1;
  uint16_t ADSR2;
  uint32_t artID;
  int sample_num;

  AkaoArt() : unityKey(0), fineTune(0), sample_offset(0), loop_point(0), ADSR1(0), ADSR2(0), artID(0), sample_num(0){}
};


class AkaoSampColl final :
    public VGMSampColl {
 public:
   AkaoSampColl(RawFile *file, uint32_t offset, AkaoPs1Version version, std::wstring name = L"Akao Sample Collection");
   AkaoSampColl(RawFile *file, AkaoInstrDatLocation file_location, std::wstring name = L"Akao Sample Collection");

  bool GetHeaderInfo() override;
  bool GetSampleInfo() override;

  [[nodiscard]] AkaoPs1Version version() const noexcept { return version_; }

  [[nodiscard]] static bool IsPossibleAkaoSampColl(RawFile *file, uint32_t offset);
  [[nodiscard]] static AkaoPs1Version GuessVersion(RawFile *file, uint32_t offset);

  std::vector<AkaoArt> akArts;
  uint32_t starting_art_id;
  uint16_t sample_set_id;

 private:
  AkaoPs1Version version_;
  uint32_t sample_section_size;
  uint32_t nNumArts;
  uint32_t arts_offset;
  uint32_t sample_section_offset;
  AkaoInstrDatLocation file_location;
};


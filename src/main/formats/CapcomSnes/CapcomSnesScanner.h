#pragma once
#include "Scanner.h"
#include "BytePattern.h"
#include "ExtensionDiscriminator.h"

enum CapcomSnesVersion: uint8_t; // see CapcomSnesFormat.h

class CapcomSnesScanner:
    public VGMScanner {
 public:
  CapcomSnesScanner() {
    USE_EXTENSION("spc");
  }
  ~CapcomSnesScanner() override {}

  void Scan(RawFile *file, void *info = nullptr) override;
  void SearchForCapcomSnesFromARAM(RawFile *file) const;
  static void SearchForCapcomSnesFromROM(RawFile *file);

 private:
  static int GetLengthOfSongList(const RawFile *file, uint16_t addrSongList);
  static uint16_t GetCurrentPlayAddressFromARAM(const RawFile *file, CapcomSnesVersion version, uint8_t channel);
  int8_t GuessCurrentSongFromARAM(const RawFile *file, CapcomSnesVersion version, uint16_t addrSongList) const;
  static bool IsValidBGMHeader(const RawFile *file, uint32_t addrSongHeader);
  static std::map<uint8_t, uint8_t> GetInitDspRegMap(const RawFile *file);

  static BytePattern ptnReadSongList;
  static BytePattern ptnReadBGMAddress;
  static BytePattern ptnDspRegInit;
  static BytePattern ptnDspRegInitOldVer;
  static BytePattern ptnLoadInstrTableAddress;
};

#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum CapcomSnesVersion: uint8_t; // see CapcomSnesFormat.h

class CapcomSnesScanner:
    public VGMScanner {
 public:
  explicit CapcomSnesScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info = nullptr) override;
  void searchForCapcomSnesFromARAM(RawFile *file) const;

 private:
  static int getLengthOfSongList(const RawFile *file, uint16_t addrSongList);
  static uint16_t getCurrentPlayAddressFromARAM(const RawFile *file, CapcomSnesVersion version, uint8_t channel);
  int8_t guessCurrentSongFromARAM(const RawFile *file, CapcomSnesVersion version, uint16_t addrSongList) const;
  static bool isValidBGMHeader(const RawFile *file, uint32_t addrSongHeader);
  static std::map<uint8_t, uint8_t> getInitDspRegMap(const RawFile *file);

  static BytePattern ptnReadSongList;
  static BytePattern ptnReadBGMAddress;
  static BytePattern ptnDspRegInit;
  static BytePattern ptnDspRegInitOldVer;
  static BytePattern ptnLoadInstrTableAddress;
};

#pragma once

#include "base/Types.h"
#include "Scanner.h"
#include "BytePattern.h"

enum CapcomSnesVersion: u8; // see CapcomSnesFormat.h

class CapcomSnesScanner:
    public VGMScanner {
 public:
  explicit CapcomSnesScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info = nullptr) override;
  void searchForCapcomSnesFromARAM(RawFile *file) const;

 private:
  static int getLengthOfSongList(const RawFile *file, u16 addrSongList);
  static u16 getCurrentPlayAddressFromARAM(const RawFile *file, CapcomSnesVersion version, u8 channel);
  s8 guessCurrentSongFromARAM(const RawFile *file, CapcomSnesVersion version, u16 addrSongList) const;
  static bool isValidBGMHeader(const RawFile *file, u32 addrSongHeader);
  static std::map<u8, u8> getInitDspRegMap(const RawFile *file);

  static BytePattern ptnReadSongList;
  static BytePattern ptnReadBGMAddress;
  static BytePattern ptnDspRegInit;
  static BytePattern ptnDspRegInitOldVer;
  static BytePattern ptnLoadInstrTableAddress;
};

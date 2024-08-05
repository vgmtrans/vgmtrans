/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMTag.h"
#include "common.h"
#include <array>

class RawFile;
class SPCFile;

struct ID666Tag {
  std::string songTitle;
  std::string gameTitle;
  std::string nameOfDumper;
  std::string comments;
  std::string dateDumped;
  u32 secondsToPlay;
  u32 fadeLength;
  std::string artist;
  u8 defaultChannelDisables;
  u8 emulatorUsed;
};

VGMTag tagFromSPCFile(const SPCFile&);

class SPCFile {
public:
  explicit SPCFile(const RawFile& file);
  ~SPCFile() = default;

  [[nodiscard]] const std::array<u8, 64 * 1024>& ram() const { return m_ram; }
  [[nodiscard]] const std::array<u8, 128>& dsp() const { return m_dspRegisters; }
  [[nodiscard]] const std::array<u8, 64>& extraRam() const { return m_extraRam; }
  [[nodiscard]] const ID666Tag& id666Tag() const { return m_id666Tag; }

private:
  bool m_hasID666Tag;
  uint8_t m_versionMinor;
  std::array<u8, 64 * 1024> m_ram;
  std::array<u8, 128> m_dspRegisters;
  std::array<u8, 64> m_extraRam;
  ID666Tag m_id666Tag;

  void loadID666Tag(const RawFile& file);
  void loadExtendedID666Tag(const RawFile& file, size_t offset);
};
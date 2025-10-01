/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <string>

#include "VGMSeq.h"
#include "SeqTrack.h"

class NinN64Seq : public VGMSeq {
 public:
  NinN64Seq(RawFile *file, uint32_t offset, std::string name = "Nin N64 Seq");
  ~NinN64Seq() override = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;

 private:
  static constexpr size_t kMaxTracks = 16;
};

class NinN64Track : public SeqTrack {
 public:
  NinN64Track(NinN64Seq *parentSeq, uint32_t offset, const std::string &name);

  void resetVars() override;
  bool readEvent() override;

 private:
  uint8_t readByte();
  uint32_t readVarLen();

  uint8_t runningStatus;
};

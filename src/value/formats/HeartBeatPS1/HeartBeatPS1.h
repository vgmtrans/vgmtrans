/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::heartbeat_ps1 {

inline constexpr std::string_view kHeartBeatPs1FormatName = "HeartBeatPS1";
inline constexpr std::string_view kHeartBeatPs1InstrumentDomain = "heartbeat-ps1.instrument";
inline constexpr std::string_view kHeartBeatPs1CommandKindPrefix = "heartbeat-ps1:sequence";

[[nodiscard]] inline core::InstrumentIdentity heartBeatPs1InstrumentIdentity(u16 bank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kHeartBeatPs1InstrumentDomain),
      .key = (static_cast<u32>(bank) << 8) | program,
  };
}

struct HeartBeatPs1EventLayout {
  u32 offset = 0;
  u32 end = 0;
  u32 delta = 0;
  u8 deltaSize = 0;
  u8 status = 0;
  bool explicitStatus = false;
  u8 data1 = 0;
  u8 data2 = 0;
  u32 dataBytes = 0;
  std::optional<u32> loopDestination;
  u8 loopCount = 0;
};

struct HeartBeatPs1SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u32 qQesOffset = 0;
  u32 dataOffset = 0;
  u32 dataEnd = 0;
  u16 sequenceId = 0;
  u16 version = 0;
  u16 ppqn = 480;
  u32 initialTempo = 500000;
  u8 rhythmNumerator = 4;
  u8 rhythmDenominatorPower = 2;
  u8 trackCount = 0;
  std::array<u16, 4> bankIds{0xffff, 0xffff, 0xffff, 0xffff};
  std::vector<HeartBeatPs1EventLayout> events;
};

struct HeartBeatPs1BankLayout {
  u32 containerOffset = 0;
  u32 sampleOffset = 0;
  u32 sampleSize = 0;
  u32 attributeOffset = 0;
  u32 attributeSize = 0;
  u16 bank = 0xffff;
  u8 slot = 0;
  u8 programCount = 0;
  u16 toneCount = 0;
  u8 masterVolume = 127;
  u8 masterPan = 64;
};

struct HeartBeatPs1ContainerLayout {
  u32 offset = 0;
  u32 length = 0;
  std::array<u16, 4> bankIds{0xffff, 0xffff, 0xffff, 0xffff};
  std::vector<HeartBeatPs1BankLayout> banks;
  std::optional<HeartBeatPs1SequenceLayout> sequence;
};

struct HeartBeatPs1Tone {
  u32 sampleOffset = 0;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  u8 volume = 127;
  u8 pan = 64;
  double unityKey = 60.0;
  u8 bendDownSemitones = 2;
  u8 bendUpSemitones = 2;
  core::KeyRange keys;
  u8 flags = 0;
  core::SourceRecord source;
};

struct HeartBeatPs1InstrumentInfo {
  u16 bank = 0xffff;
  u8 program = 0;
  std::vector<HeartBeatPs1Tone> tones;
};

struct HeartBeatPs1ScannedBank {
  core::ScanSoundBankDraft bank;
  std::vector<HeartBeatPs1InstrumentInfo> instruments;
};

[[nodiscard]] std::optional<HeartBeatPs1ContainerLayout> readHeartBeatPs1Container(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<HeartBeatPs1ContainerLayout> findHeartBeatPs1Containers(core::ByteReader reader);
[[nodiscard]] std::optional<HeartBeatPs1ScannedBank> addHeartBeatPs1Bank(core::ScanResultBuilder& result,
                                                                         const HeartBeatPs1BankLayout& layout);
[[nodiscard]] core::SequenceProgram parseHeartBeatPs1Sequence(
    core::ByteReader reader, core::AssetId id, const HeartBeatPs1SequenceLayout& layout,
    const std::vector<HeartBeatPs1InstrumentInfo>& instruments = {}, core::SourceMapBuilder* sourceMap = nullptr,
    std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& heartBeatPs1SequenceConfig();
[[nodiscard]] core::FormatModule heartBeatPs1Module();

}  // namespace vgmtrans::formats::heartbeat_ps1

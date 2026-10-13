/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/OceanSnes/OceanSnes.h"
#include "value/sequence/SequenceVm.h"

namespace vgmtrans::formats::ocean_snes {

struct RuntimeConfig {
  core::RetainedSource source;
  Layout layout;
  std::shared_ptr<const DriverData> data;
};

struct Voice {
  struct PendingKeyOn {
    core::PerformanceEmitter output;
    u8 ticks = 2;
  };

  const InstrumentRegion* region = nullptr;
  u8 regionProgram = 0, regionKey = 0x81;  // Export selection; survives a region miss.
  u8 instrumentVolume = 0, instrumentTranspose = 0;
  u8 eventVolume = 0, trackVolume = 0, regionVolume = 0;
  u16 pan = 0x4000, panStep = 0;
  u8 panTicks = 0, surround = 0;
  s8 volumeStep = 0, bendStep = 0;
  s16 bend = 0;
  u8 sensitivity = 1, bendUnit = 0;
  u16 pitch = 0;
  u8 pitchFraction = 0, pendingNote = 0;
  bool calculatePitch = false;
  u16 slideTarget = 0, slideDepth = 0;
  s32 slideStep = 0;
  u8 slideTicks = 0;
  u16 vibratoDelay = 0, vibratoDepth = 0;
  u8 vibratoAmount = 0, vibratoRate = 0, vibratoPhase = 0;
  u8 lfoTarget = 0;
  s16 lfoPitch = 0;
  bool volumeDirty = true;
  std::optional<s32> noteKey;  // Kept after release so controls still affect its tail.
  bool gateOpen = false;
  bool muted = false;
  std::optional<PendingKeyOn> pendingKeyOn;
  std::optional<u64> automaticRelease;
  std::optional<double> emittedBend;
  std::optional<std::array<s8, 2>> emittedBalance;

  void start(const RuntimeConfig& config, u8 program, u8 encodedNote, u16& random, core::PerformanceEmitter& out,
             core::VmApi& vm);
  void release(core::PerformanceEmitter& out);
  void portamento(const RuntimeConfig& config, u8 encodedNote, u8 duration, u8 transpose);
  void tick(const RuntimeConfig& config, u8 transpose, u8 masterVolume, u8 musicVolume, u8 outputMode,
            core::PerformanceEmitter& out);
  void panTo(u8 target, u8 logicalSlot);
  void changeVibratoDepth(u8 delta);
  void applyLfo(int delta);

private:
  [[nodiscard]] u8 noteIndex(u8 encodedNote, u8 transpose, bool clampNegative = true) const;
  [[nodiscard]] u16 pitchAt(const RuntimeConfig& config, u8 index) const;
  [[nodiscard]] u8 sensitivityAt(const RuntimeConfig& config, u8 index) const;
  [[nodiscard]] u16 advancePitch(const RuntimeConfig& config);
};

}  // namespace vgmtrans::formats::ocean_snes

#include "PSDSEPS2Seq.h"

#include "LogManager.h"
#include "PSDSEFormat.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>

#include <fmt/format.h>

namespace {

// [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: SsdSeqFuncTrap, its matching handlers, and the
// shipped SsdSeqFuncLength table define the version 0x0301 names and operand widths.
// [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: The driver establishes the version 0x0300 width changes.
// [Tokimeki Memorial: Girl's Side 2nd Kiss]: The driver establishes the version 0x0320 width changes.
constexpr std::array<uint8_t, 128> kOperandCounts = {
    0, 0, 1, 1, 1, 2, 3, 4, 2, 2, 2, 0, 3, 2, 3, 0,  // 80-8f
    0, 0, 2, 3, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 2, 2,  // 90-9f
    2, 3, 1, 1, 1, 1, 3, 0, 1, 1, 1, 3, 1, 1, 3, 0,  // a0-af
    0, 3, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1,  // b0-bf
    0, 0, 0, 0, 1, 1, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0,  // c0-cf
    1, 1, 1, 2, 2, 3, 2, 1, 3, 3, 0, 0, 0, 0, 0, 1,  // d0-df
    1, 1, 3, 1, 3, 3, 0, 0, 1, 1, 3, 1, 3, 3, 0, 0,  // e0-ef
    3, 3, 2, 0, 0, 0, 1, 1, 0, 1, 3, 2, 2, 5, 1, 0,  // f0-ff
};

uint8_t operandCount(uint8_t status, uint16_t version) {
  if (PSDSEPS2::isV2(version)) {
    // [Shadow Hearts]: The named handlers define these older widths. Disabled Jump, If, Random Note, Priority,
    // Sustain, and Label slots point to SsdSeqDummy; All Reset consumes three authoring parameters.
    switch (status) {
      case 0x88:
      case 0x89:
        return 1;
      case 0x92:
      case 0x93:
      case 0xae:
      case 0xbb:
      case 0xbf:
      case 0xf9:
        return 0;
      case 0xdf:
        return 2;
      case 0xf8:
        return 3;
      default:
        break;
    }
  }
  if (status == 0xf8 && version == 0x0300) {
    return 3;
  }
  if (version == 0x0320) {
    switch (status) {
      case 0x87:
      case 0xa7:
      case 0xca:
      case 0xcb:
      case 0xd6:
        return 1;
      case 0xa8:
      case 0xaf:
        return 2;
      case 0xab:
        return 1;
      case 0xad:
        return 3;
      default:
        break;
    }
  }
  return kOperandCounts[status - 0x80];
}

std::string eventName(uint8_t status, uint16_t version) {
  switch (status) {
    case 0x80:
      return "Delta For Gate";
    case 0x81:
      return "Delta For After Delta";
    case 0x82:
      return "Delta Add Gate";
    case 0x83:
      return "Delta Add After Delta";
    case 0x84:
      return "Delta 1";
    case 0x85:
      return "Delta 2";
    case 0x86:
      return "Delta 3";
    case 0x87:
      return version == 0x0320 ? "Callback Wait" : "Dummy 0x87";
    case 0x88:
      return "Rest";
    case 0x89:
      return "Tie";
    case 0x90:
      return "Stop";
    case 0x91:
      return "Repeat";
    case 0x92:
      return PSDSEPS2::isV2(version) ? "Dummy 0x92" : "Jump";
    case 0x93:
      return PSDSEPS2::isV2(version) ? "Dummy 0x93" : "If";
    case 0x94:
      return "Octave Absolute";
    case 0x95:
      return "Octave Relative";
    case 0x96:
      return "Octave Up";
    case 0x97:
      return "Octave Down";
    case 0x98:
      return "Loop Top";
    case 0x99:
      return "Loop End";
    case 0x9a:
      return "Loop Escape";
    case 0x9c:
      return "Tempo Absolute";
    case 0x9d:
      return "Tempo Relative";
    case 0xa0:
      return "Measure";
    case 0xa1:
      return "Position";
    case 0xa4:
      return "Cue Point";
    case 0xa6:
      return "Master Fader";
    case 0xa7:
      return version == 0x0320 ? "Voice Limit" : "Dummy 0xA7";
    case 0xa8:
      return version == 0x0320 ? "Bank Absolute" : (PSDSEPS2::isV2(version) ? "Bank Select" : "Bank MSB");
    case 0xa9:
      return version == 0x0320 ? "Bank MSB" : (PSDSEPS2::isV2(version) ? "Type Select" : "Bank LSB");
    case 0xaa:
      return version == 0x0320 ? "Bank LSB" : "Wave Change";
    case 0xab:
      return version == 0x0320 ? "Wave Change" : "Wave Bank Type";
    case 0xac:
      return "Program Change";
    case 0xad:
      return version == 0x0300 ? "Dummy 0xAD" : "Voice Mode";
    case 0xae:
      return PSDSEPS2::isV2(version) ? "Dummy 0xAE" : "Random Note";
    case 0xaf:
      return version == 0x0320 ? "Random Note Range" : "Dummy 0xAF";
    case 0xb0:
      return "ADSR Reset";
    case 0xb1:
      return "ADSR Mode";
    case 0xb2:
      return "Attack Rate";
    case 0xb3:
      return "Decay Rate";
    case 0xb4:
      return "Sustain Level";
    case 0xb5:
      return "Sustain Rate";
    case 0xb6:
      return "Release Rate";
    case 0xb7:
      return "Decay Rate And Sustain Level";
    case 0xb8:
      return "Attack Mode";
    case 0xb9:
      return "Sustain Mode";
    case 0xba:
      return "Release Mode";
    case 0xbb:
      return PSDSEPS2::isV2(version) ? "Dummy 0xBB" : "Priority";
    case 0xbc:
      return "Gate Time";
    case 0xbd:
      return version == 0x0300 ? "Dummy 0xBD" : "Step Relative";
    case 0xbe:
      return version == 0x0300 ? "Dummy 0xBE" : "Modulation";
    case 0xbf:
      return PSDSEPS2::isV2(version) ? "Dummy 0xBF" : "Sustain";
    case 0xc0:
      return "SULR On";
    case 0xc1:
      return "SULR Off";
    case 0xc2:
      return version == 0x0320 ? "Dummy 0xC2" : "PM On";
    case 0xc3:
      return version == 0x0320 ? "Dummy 0xC3" : "PM Off";
    case 0xc4:
      return "Noise Absolute";
    case 0xc5:
      return "Noise Relative";
    case 0xc6:
      return "Noise On";
    case 0xc7:
      return version == 0x0320 ? "Dummy 0xC7" : "Noise Off";
    case 0xc8:
      return "Reverb Parameter";
    case 0xc9:
      return "Reverb Set";
    case 0xca:
      return version == 0x0320 ? "SPU2 Effect 0x8000 Switch" : "Reverb On";
    case 0xcb:
      return version == 0x0320 ? "Reverb Switch" : "Reverb Off";
    case 0xd0:
      return "Key Transpose Absolute";
    case 0xd1:
      return "Key Transpose Relative";
    case 0xd2:
      return "Tune";
    case 0xd3:
      return "Detune";
    case 0xd4:
      return "Bender";
    case 0xd5:
      return "Sweep";
    case 0xd6:
      return version == 0x0320 ? "Pitch Control Parameter" : "Dummy 0xD6";
    case 0xd7:
      return "Vibrate Fade";
    case 0xd8:
      return "Vibrate 1";
    case 0xd9:
      return "Vibrate 2";
    case 0xda:
      return "Vibrate On";
    case 0xdb:
      return version == 0x0320 ? "Dummy 0xDB" : "Vibrate Off";
    case 0xdd:
      return "Bender Off";
    case 0xdf:
      return PSDSEPS2::isV2(version) ? "Dummy 0xDF" : "Expression";
    case 0xe0:
      return "Volume Absolute";
    case 0xe1:
      return "Volume Relative";
    case 0xe2:
      return "Volume Fade";
    case 0xe3:
      return "Tremolo Fade";
    case 0xe4:
      return "Tremolo 1";
    case 0xe5:
      return "Tremolo 2";
    case 0xe6:
      return "Tremolo On";
    case 0xe7:
      return version == 0x0320 ? "Dummy 0xE7" : "Tremolo Off";
    case 0xe8:
      return "Panpot Absolute";
    case 0xe9:
      return "Panpot Relative";
    case 0xea:
      return "Panpot Move";
    case 0xeb:
      return "Shake Fade";
    case 0xec:
      return "Shake 1";
    case 0xed:
      return "Shake 2";
    case 0xee:
      return "Shake On";
    case 0xef:
      return version == 0x0320 ? "Dummy 0xEF" : "Shake Off";
    case 0xf0:
      return "LFO Process";
    case 0xf1:
      return "LFO Parameter";
    case 0xf2:
      return "LFO Delay";
    case 0xf6:
      return "LFO On";
    case 0xf7:
      return "LFO Off";
    case 0xf8:
      return "All Reset";
    case 0xf9:
      return PSDSEPS2::isV2(version) ? "Dummy 0xF9" : "Label";
    case 0xfa:
      return "SMPTE Time";
    case 0xfb:
      return "SMPTE Frame";
    case 0xfd:
      return "SMPTE Offset";
    case 0xfe:
      return "Skip";
    default:
      return fmt::format("Dummy 0x{:02X}", status);
  }
}

std::string rawValueDetail(uint8_t status, const std::string& operands) {
  return operands.empty() ? fmt::format("Opcode: 0x{:02X}", status)
                          : fmt::format("Opcode: 0x{:02X}, operands: {}", status, operands);
}

}  // namespace

PSDSEPS2Seq::PSDSEPS2Seq(RawFile* file, const PSDSEPS2::SequenceHeader& header)
    : VGMSeq(PSDSEFormat::name, file, header.offset, header.fileLength, header.internalName), m_header(header) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setPPQN(48);
  setAlwaysWriteInitialTempo(120);
  setInitialVolume(127);
  // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: Jump events routinely implement backward
  // sequence loops. Control-flow state identifies the repeated state and the resulting sequence stop time.
  setShouldTrackControlFlowState(true);
}

void PSDSEPS2Seq::resetVars() {
  VGMSeq::resetVars();
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    m_peakEffectiveVelocity = 0;
  }
  // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: SSD initializes every track from the sequence
  // header wave-bank ID, including sequences without an explicit Bank MSB or Bank LSB event.
  addBankReference(m_header.defaultBankId);
}

void PSDSEPS2Seq::observeEffectVelocity() {
  if (!m_header.isEffect) {
    return;
  }
  m_peakEffectiveVelocity = std::max<uint32_t>(m_peakEffectiveVelocity, m_header.initialVoiceVelocity);
}

uint8_t PSDSEPS2Seq::normalizedEffectVelocity() const {
  if (!m_header.isEffect || m_peakEffectiveVelocity == 0) {
    return m_header.initialVoiceVelocity;
  }
  // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: SEDS effect velocity is eight-bit and can exceed
  // the MIDI seven-bit range. Values remain unchanged when the entire effect fits; otherwise one sequence-wide
  // multiplier maps its effective peak to 127 while retaining relative velocities.
  if (m_peakEffectiveVelocity <= 127) {
    return m_header.initialVoiceVelocity;
  }
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(m_header.initialVoiceVelocity) * 127 + m_peakEffectiveVelocity / 2) /
      m_peakEffectiveVelocity);
}

bool PSDSEPS2Seq::parseHeader() {
  if (m_header.isEffect) {
    const uint32_t pointerBytes = static_cast<uint32_t>(m_header.trackCount) * 2;
    auto* header = addHeader(offset(), m_header.effectHeaderSize + pointerBytes, "PS2 SEDS Effect Record");
    if (m_header.version == 0x0320) {
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: SsdPlayEffectParamData transfers +1 as flags and splits +2 into
      // low-nibble priority and high-nibble output group. SsdSaechSeqFreeTrack compares priority when stealing
      // voices, SsdPlaySeqEffectNormal applies the output group through its table, and the driver never reads +0.
      header->addChild(offset(), 1, "Effect Record Type (Driver Unused)");
      header->addChild(offset() + 1, 1, "Effect Flags");
      header->addChild(offset() + 2, 1, "Output Group And Priority");
    } else {
      // [Shadow Hearts]: Effect records use +0 as flags, +1 as initial velocity, and +2 as the priority consumed by
      // SsdSaechSeqFreeTrack.
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The version 0x0301 effect records use the same
      // fields.
      header->addChild(offset(), 1, "Effect Flags");
      header->addChild(offset() + 1, 1, "Initial Voice Velocity");
      header->addChild(offset() + 2, 1, "Effect Priority");
    }
    header->addChild(offset() + 3, 1, "Track Count");
    if (m_header.version == 0x0320) {
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 key-on path clamps bank and program allocation
      // with the low bytes of +4 and +6. SEDS records store the inclusive PS2 voice range 0x20 through 0x2f here.
      // Offset +8 mirrors the set-level bank ID, while the driver resolves the set-level field.
      header->addChild(offset() + 4, 2, "Effect Voice Range Start");
      header->addChild(offset() + 6, 2, "Effect Voice Range End");
      header->addChild(offset() + 8, 2, "Bank ID Mirror (Driver Unused)");
      header->addChild(offset() + 0x0a, 2, "Unused Effect Record Data");
      header->addChild(offset() + 0x0c, 1, "Initial Voice Velocity");
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: Every audited record uses 0x40 and either 0x2a or 0x2d here, and
      // the shipped version 0x0320 runtime never reads these authoring defaults.
      header->addChild(offset() + 0x0d, 1, "Authoring Default 1 (Driver Unused)");
      header->addChild(offset() + 0x0e, 1, "Authoring Default 2 (Driver Unused)");
      header->addChild(offset() + 0x0f, 1, "Unused Effect Record Data");
    } else {
      // [Shadow Hearts]: The unstripped version 2 driver does not read +4.
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named version 0x0301 driver does not read
      // +4, and +6 equals the record extent in audited records.
      header->addChild(offset() + 4, 2, "Unused Effect Record Data");
      header->addChild(offset() + 6, 2, "Effect Record Size");
    }
    header->addChild(offset() + m_header.effectHeaderSize, pointerBytes, "Track Offsets");
    addBankReference(m_header.defaultBankId);
    return true;
  }

  if (PSDSEPS2::isV2(m_header.version)) {
    auto* header = addHeader(offset(), 0x28, "PS2 SMDM v2 Header");
    header->addChild(offset(), 4, "Magic");
    header->addChild(offset() + 0x04, 4, "Checksum Key");
    header->addChild(offset() + 0x08, 4, "File Length");
    header->addChild(offset() + 0x0c, 2, "Version");
    // [Shadow Hearts]: SsdAddSequenceData and SsdInitSequence never read this header halfword, and every audited
    // sequence stores zero here.
    header->addChild(offset() + 0x0e, 2, "Zero Padding");
    header->addChild(offset() + 0x10, 8, "Creation Timestamp");
    header->addChild(offset() + 0x18, 2, "File ID");
    // [Shadow Hearts]: SsdAddSequenceData seeds the note accumulator from +0x1a. The note decoder adds its encoded
    // octave and key before SsdSeqKeyonVoice consumes it.
    header->addChild(offset() + 0x1a, 1, "Initial Note Base");
    header->addChild(offset() + 0x1b, 1, "Zero Padding");
    header->addChild(offset() + 0x1c, 2, "Authoring Metadata (Driver Unused)");
    header->addChild(offset() + 0x1e, 2, "Default Wave Bank ID");
    header->addChild(offset() + 0x20, 1, "Track Count");
    header->addChild(offset() + 0x21, 1, "Channel Count");
    // [Shadow Hearts]: This byte stores tempo-like authoring values, but the shipped driver never reads it; tempo
    // changes are encoded as runtime sequence events.
    header->addChild(offset() + 0x22, 1, "Authoring Timing Metadata (Driver Unused)");
    header->addChild(offset() + 0x23, 1, "Highest Voice Number");
    header->addChild(offset() + 0x24, 1, "Reverb Mode");
    header->addChild(offset() + 0x25, 1, "Reverb Depth");
    header->addChild(offset() + 0x26, 1, "Reverb Delay");
    header->addChild(offset() + 0x27, 1, "Reverb Feedback");
    addBankReference(m_header.defaultBankId);
    return true;
  }

  auto* header = addHeader(offset(), 0x50, "PS2 SMDM Header");
  header->addChild(offset(), 4, "Magic");
  header->addChild(offset() + 0x04, 4, "Checksum Key");
  header->addChild(offset() + 0x08, 4, "File Length");
  if (m_header.version == 0x0300) {
    header->addChild(offset() + 0x0c, 4, "Checksum");
    header->addChild(offset() + 0x10, 2, "Version");
    header->addChild(offset() + 0x12, 2, "File ID");
    // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: Version 0x0300 SsdAddSequenceData and SsdInitSequence
    // do not read this authoring-data range.
    header->addChild(offset() + 0x14, 0x0c, "Authoring Metadata (Driver Unused)");
  } else {
    header->addChild(offset() + 0x0c, 2, "Version");
    header->addChild(offset() + 0x0e, 2, "File ID");
    header->addChild(offset() + 0x10, 4, "Checksum");
    header->addChild(offset() + 0x14, 4, "Checksum Coverage");
    header->addChild(offset() + 0x18, 8, "Creation Timestamp");
  }
  header->addChild(offset() + 0x20, 16, "Internal Name");
  header->addChild(offset() + 0x30, 2, "Sequence Flags");
  header->addChild(offset() + 0x32, 2, "Default Wave Bank ID");
  // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named version 0x0301 routines read +0x38,
  // +0x3c, +0x44, +0x45, and +0x48 through +0x4f during sequence creation. Offset +0x39 is copied to runtime state
  // but never consumed.
  // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The structurally matching version 0x0320 routines read the same
  // fields.
  header->addChild(offset() + 0x34, 4, "Authoring Timing Metadata (Driver Unused)");
  header->addChild(offset() + 0x38, 1, "Device Priority");
  header->addChild(offset() + 0x39, 1, "Authoring Metadata (Driver Unused)");
  header->addChild(offset() + 0x3a, 2, "Unused Sequence Data");
  header->addChild(offset() + 0x3c, 1, "Output Group");
  header->addChild(offset() + 0x3d, 3, "Authoring Metadata (Driver Unused)");
  header->addChild(offset() + 0x40, 1, "Track Count");
  header->addChild(offset() + 0x41, 1, "Channel Count");
  header->addChild(offset() + 0x42, 2, "Unused Sequence Data");
  header->addChild(offset() + 0x44, 1, "First Voice Number");
  header->addChild(offset() + 0x45, 1, "Highest Voice Number");
  header->addChild(offset() + 0x46, 2, "Unused Sequence Data");
  header->addChild(offset() + 0x48, 1, "Core 0 Reverb Mode");
  header->addChild(offset() + 0x49, 1, "Core 0 Reverb Depth");
  header->addChild(offset() + 0x4a, 1, "Core 0 Reverb Delay");
  header->addChild(offset() + 0x4b, 1, "Core 0 Reverb Feedback");
  header->addChild(offset() + 0x4c, 1, "Core 1 Reverb Mode");
  header->addChild(offset() + 0x4d, 1, "Core 1 Reverb Depth");
  header->addChild(offset() + 0x4e, 1, "Core 1 Reverb Delay");
  header->addChild(offset() + 0x4f, 1, "Core 1 Reverb Feedback");
  addBankReference(m_header.defaultBankId);
  return true;
}

bool PSDSEPS2Seq::parseTrackPointers() {
  for (const auto& record : m_header.tracks) {
    if (m_header.isEffect) {
      addTrack<PSDSEPS2Track>(this, record);
      continue;
    }
    if (PSDSEPS2::isV2(m_header.version)) {
      auto* trackHeader = addHeader(record.recordOffset, 8, "Track Record");
      trackHeader->addChild(record.recordOffset, 2, "Record Type");
      trackHeader->addChild(record.recordOffset + 2, 2, "Record Size");
      trackHeader->addChild(record.recordOffset + 4, 1, "Voice Count");
      trackHeader->addChild(record.recordOffset + 5, 1, "Track ID");
      trackHeader->addChild(record.recordOffset + 6, 1, "Channel");
      trackHeader->addChild(record.recordOffset + 7, 1, "Output Group");
      addTrack<PSDSEPS2Track>(this, record);
      continue;
    }
    auto* trackHeader = addHeader(record.recordOffset, 0x14, "Track Record");
    trackHeader->addChild(record.recordOffset, 4, "Record Type");
    const uint32_t sizeFieldOffset = m_header.version == 0x0320 ? 0x04 : 0x08;
    trackHeader->addChild(record.recordOffset + sizeFieldOffset, 4, "Aligned Record Size");
    trackHeader->addChild(record.recordOffset + sizeFieldOffset + 4, 4, "Logical Record Size");
    if (m_header.version == 0x0320) {
      trackHeader->addChild(record.recordOffset + 0x0c, 4, "Record Control");
    }
    trackHeader->addChild(record.recordOffset + 0x10, 1, "Track Flags");
    trackHeader->addChild(record.recordOffset + 0x11, 1, "Track ID");
    trackHeader->addChild(record.recordOffset + 0x12, 1, "Channel");
    trackHeader->addChild(record.recordOffset + 0x13, 1, "Output Group");
    addTrack<PSDSEPS2Track>(this, record);
  }
  return hasTracks();
}

PSDSEPS2Track::PSDSEPS2Track(PSDSEPS2Seq* sequence, const PSDSEPS2::SequenceTrackRecord& record)
    : SeqTrack(sequence, record.eventOffset, record.eventLength), m_version(sequence->m_header.version),
      m_defaultBankId(sequence->m_header.defaultBankId), m_channel(record.channel), m_outputGroup(record.outputGroup) {
  resetVars();
}

void PSDSEPS2Track::setChannelAndGroupFromTrkNum(int) {
  channel = m_channel & 0x0f;
  channelGroup = m_outputGroup != 0 ? m_outputGroup : m_channel >> 4;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->setChannelGroup(channelGroup);
  }
}

void PSDSEPS2Track::resetVars() {
  SeqTrack::resetVars();
  const bool isEffect = static_cast<PSDSEPS2Seq*>(parentSeq)->m_header.isEffect;
  m_bankId = m_defaultBankId;
  m_initialBankPending = true;
  m_octave = isEffect ? 5 : 4;
  m_afterDelta = 0;
  m_gateDelta = 0;
  m_lastNoteDuration = 0;
  m_gateTime = isEffect ? 15 : 127;
  m_repeatOffset = 0;
  vol = 127;
  expression = 127;
  prevPan = 64;
}

uint16_t PSDSEPS2Track::readEventU16LE() {
  const uint16_t value = readByte(curOffset) | (readByte(curOffset + 1) << 8);
  curOffset += 2;
  return value;
}

uint32_t PSDSEPS2Track::readEventU24LE() {
  const uint32_t value = readByte(curOffset) | (readByte(curOffset + 1) << 8) | (readByte(curOffset + 2) << 16);
  curOffset += 3;
  return value;
}

std::string PSDSEPS2Track::readOperands(uint8_t count) {
  std::string output;
  for (uint8_t index = 0; index < count; ++index) {
    if (index != 0) {
      output += ' ';
    }
    fmt::format_to(std::back_inserter(output), "{:02X}", readByte(curOffset++));
  }
  return output;
}

void PSDSEPS2Track::addDelta(uint32_t eventOffset, uint32_t eventLength, uint32_t ticks, const std::string& name) {
  addGenericEvent(eventOffset, eventLength, name, fmt::format("Ticks: {}", ticks), VGMItem::Type::Rest);
  addTime(ticks);
}

bool PSDSEPS2Track::readEvent() {
  const uint32_t beginOffset = curOffset;
  const uint32_t trackEnd = offset() + length();
  if (curOffset >= trackEnd) {
    return false;
  }

  if (m_initialBankPending) {
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The header bank initializes runtime channel
    // state before the first sequence event. The MIDI bank event follows SeqTrack conversion-context reset.
    addBankSelectNoItem(m_defaultBankId);
    m_initialBankPending = false;
  }

  const uint8_t status = readByte(curOffset++);
  if (status < 0x80) {
    auto* sequence = static_cast<PSDSEPS2Seq*>(parentSeq);
    if (sequence->m_header.isEffect) {
      if (curOffset + 2 > trackEnd) {
        return false;
      }

      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named SsdEffectTrackSequence uses a SEDS
      // note layout where the opcode low nibble is added to the octave base and a little-endian 16-bit duration
      // follows. SsdPlaySeqEffectNormal initializes note velocity from effect record +1.
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: Version 0x0320 initializes note velocity from effect record +0xc.
      const uint32_t duration = readEventU16LE();
      uint32_t gateDuration;
      if (m_gateTime == 15) {
        gateDuration = duration > 2 ? duration - 2 : 1;
      } else if (m_gateTime == 16) {
        gateDuration = duration;
      } else {
        gateDuration = std::max<uint32_t>((duration * m_gateTime) >> 4, 1);
      }

      const int key = m_octave * 12 + (status & 0x0f);
      sequence->observeEffectVelocity();
      const uint8_t velocity = readMode == READMODE_CONVERT_TO_MIDI ? sequence->normalizedEffectVelocity()
                                                                    : sequence->m_header.initialVoiceVelocity;
      addNoteByDur(beginOffset, curOffset - beginOffset, static_cast<uint8_t>(std::clamp(key, 0, 127)), velocity,
                   gateDuration);
      addTime(duration);
      return true;
    }

    if (curOffset >= trackEnd) {
      return false;
    }
    const uint8_t flags = readByte(curOffset++);
    const uint8_t durationBytes = flags >> 6;
    if (curOffset + durationBytes > trackEnd) {
      return false;
    }

    m_octave += static_cast<int8_t>(((flags >> 4) & 3) - 1);
    uint32_t duration = m_lastNoteDuration;
    if (durationBytes != 0) {
      duration = 0;
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: SsdNormalTrackSequence shifts compact note
      // duration high-byte first, independently of the little-endian container.
      for (uint8_t byte = 0; byte < durationBytes; ++byte) {
        duration = (duration << 8) | readByte(curOffset++);
      }
      m_lastNoteDuration = duration;
    }
    m_gateDelta = duration;
    const uint32_t scaledDuration = static_cast<uint32_t>((static_cast<uint64_t>(duration) * m_gateTime) / 127);
    const int key = m_octave * 12 + (flags & 0x0f);
    addNoteByDur(beginOffset, curOffset - beginOffset, static_cast<uint8_t>(std::clamp(key, 0, 127)), status,
                 scaledDuration);
    return true;
  }

  const uint8_t count = operandCount(status, m_version);
  if (curOffset + count > trackEnd) {
    return false;
  }

  switch (status) {
    case 0x80:
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: SsdSeqDeltaForGate copies the compact note
      // duration into the saved after-delta value and the active track delay.
      m_afterDelta = m_gateDelta;
      addDelta(beginOffset, 1, m_afterDelta, eventName(status, m_version));
      break;
    case 0x81:
      addDelta(beginOffset, 1, m_afterDelta, eventName(status, m_version));
      break;
    case 0x82: {
      const int64_t delta = static_cast<int64_t>(m_afterDelta) + static_cast<int8_t>(readByte(curOffset++));
      m_afterDelta = static_cast<uint32_t>(std::max<int64_t>(0, delta));
      addDelta(beginOffset, curOffset - beginOffset, m_afterDelta, eventName(status, m_version));
      break;
    }
    case 0x83: {
      const int64_t delta = static_cast<int64_t>(m_gateDelta) + static_cast<int8_t>(readByte(curOffset++));
      m_afterDelta = static_cast<uint32_t>(std::max<int64_t>(0, delta));
      addDelta(beginOffset, curOffset - beginOffset, m_afterDelta, eventName(status, m_version));
      break;
    }
    case 0x84:
      m_afterDelta = readByte(curOffset++);
      addDelta(beginOffset, curOffset - beginOffset, m_afterDelta, eventName(status, m_version));
      break;
    case 0x85:
      m_afterDelta = readEventU16LE();
      addDelta(beginOffset, curOffset - beginOffset, m_afterDelta, eventName(status, m_version));
      break;
    case 0x86:
      m_afterDelta = readEventU24LE();
      addDelta(beginOffset, curOffset - beginOffset, m_afterDelta, eventName(status, m_version));
      break;
    case 0x87: {
      if (m_version != 0x0320) {
        const std::string operands = readOperands(count);
        addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                        rawValueDetail(status, operands), VGMItem::Type::Unknown);
        break;
      }
      const uint8_t pollDelay = readByte(curOffset++);
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 handler repeatedly invokes the channel
      // callback and reprocesses this opcode until callback state clears. The operand becomes track delay, while
      // total wait depends on the game callback.
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Callback poll delay: {}; conversion continues immediately", pollDelay),
                      VGMItem::Type::Unknown);
      break;
    }
    case 0x88: {
      const uint32_t duration = PSDSEPS2::isV2(m_version) ? readByte(curOffset++) : readEventU16LE();
      addRest(beginOffset, curOffset - beginOffset, duration, eventName(status, m_version));
      break;
    }
    case 0x89: {
      const uint32_t duration = PSDSEPS2::isV2(m_version) ? readByte(curOffset++) : readEventU16LE();
      addTie(beginOffset, curOffset - beginOffset, duration, eventName(status, m_version));
      makePrevDurNoteEnd(getTime() + duration);
      addTime(duration);
      break;
    }
    case 0x90:
      if (m_repeatOffset != 0) {
        const bool shouldContinue = addLoopForever(beginOffset, 1, "Stop / Repeat");
        curOffset = m_repeatOffset;
        return shouldContinue;
      }
      addEndOfTrack(beginOffset, 1);
      return false;
    case 0x91:
      m_repeatOffset = curOffset;
      m_afterDelta = 0;
      m_gateDelta = 0;
      addGenericEvent(beginOffset, 1, "Repeat Start", "", VGMItem::Type::RepeatStart);
      break;
    case 0x92: {
      if (PSDSEPS2::isV2(m_version)) {
        addGenericEvent(beginOffset, 1, eventName(status, m_version), rawValueDetail(status, ""),
                        VGMItem::Type::Unknown);
        break;
      }
      const int16_t displacement = static_cast<int16_t>(readEventU16LE());
      const int64_t target = static_cast<int64_t>(beginOffset) + displacement;
      if (target < offset() || target >= trackEnd) {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Jump", fmt::format("Invalid destination: {}", target),
                        VGMItem::Type::Unknown);
        return false;
      }
      return addJump(beginOffset, curOffset - beginOffset, static_cast<uint32_t>(target), "Jump");
    }
    case 0x93: {
      if (PSDSEPS2::isV2(m_version)) {
        addGenericEvent(beginOffset, 1, eventName(status, m_version), rawValueDetail(status, ""),
                        VGMItem::Type::Unknown);
        break;
      }
      const uint8_t condition = readByte(curOffset++);
      const int16_t displacement = static_cast<int16_t>(readEventU16LE());
      addGenericEvent(beginOffset, curOffset - beginOffset, "If",
                      fmt::format("Runtime condition: {}, relative destination: {}; conversion follows fallthrough",
                                  condition, displacement),
                      VGMItem::Type::Unknown);
      break;
    }
    case 0x94:
      m_octave = static_cast<int8_t>(readByte(curOffset++));
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Octave: {}", m_octave), VGMItem::Type::Unknown);
      break;
    case 0x95:
      m_octave += static_cast<int8_t>(readByte(curOffset++));
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Octave: {}", m_octave), VGMItem::Type::Unknown);
      break;
    case 0x96:
    case 0x97:
      m_octave += status == 0x96 ? 1 : -1;
      addGenericEvent(beginOffset, 1, eventName(status, m_version), fmt::format("Octave: {}", m_octave),
                      VGMItem::Type::Unknown);
      break;
    case 0x9c:
    case 0x9d: {
      const uint8_t value = readByte(curOffset++);
      const double bpm = status == 0x9c ? value : parentSeq->tempoBPM + value;
      addTempoBPM(beginOffset, curOffset - beginOffset, bpm, eventName(status, m_version));
      break;
    }
    case 0xa6: {
      const uint16_t duration = readEventU16LE();
      const uint8_t target = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Target: {}, duration: {}; master-output automation stub", target, duration),
                      VGMItem::Type::Unknown);
      break;
    }
    case 0xa7: {
      if (m_version != 0x0320) {
        const std::string operands = readOperands(count);
        addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                        rawValueDetail(status, operands), VGMItem::Type::Unknown);
        break;
      }
      const uint8_t limit = readByte(curOffset++);
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 key-on path compares this channel value with
      // the program Voice Limit field before allocating a voice.
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Channel voice limit: {}", limit), VGMItem::Type::Unknown);
      break;
    }
    case 0xa8: {
      if (m_version == 0x0320) {
        // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 dispatcher adds a two-byte absolute-bank
        // command before the version 0x0301 SsdSeqBankMSB and SsdSeqBankLSB pair. It reads the value little-endian
        // and resolves it against the SWDM common-header file ID.
        m_bankId = readEventU16LE();
      } else {
        const uint8_t value = readByte(curOffset++);
        m_bankId = PSDSEPS2::isV2(m_version) ? static_cast<uint16_t>((m_bankId & 0xff00) | value)
                                             : static_cast<uint16_t>((m_bankId & 0x00ff) | (value << 8));
      }
      if (m_bankId > 0x3fff) {
        L_WARN("PSDSE PS2: bank ID {:#06x} exceeds the MIDI/DLS 14-bit bank range", m_bankId);
      }
      addBankSelect(beginOffset, curOffset - beginOffset, m_bankId, eventName(status, m_version));
      break;
    }
    case 0xa9: {
      const uint8_t value = readByte(curOffset++);
      m_bankId = (m_version == 0x0320 || PSDSEPS2::isV2(m_version))
                     ? static_cast<uint16_t>((m_bankId & 0x00ff) | (value << 8))
                     : static_cast<uint16_t>((m_bankId & 0xff00) | value);
      if (m_bankId > 0x3fff) {
        L_WARN("PSDSE PS2: bank ID {:#06x} exceeds the MIDI/DLS 14-bit bank range", m_bankId);
      }
      addBankSelect(beginOffset, curOffset - beginOffset, m_bankId, eventName(status, m_version));
      break;
    }
    case 0xaa: {
      if (m_version == 0x0320) {
        const uint8_t value = readByte(curOffset++);
        m_bankId = static_cast<uint16_t>((m_bankId & 0xff00) | value);
        if (m_bankId > 0x3fff) {
          L_WARN("PSDSE PS2: bank ID {:#06x} exceeds the MIDI/DLS 14-bit bank range", m_bankId);
        }
        addBankSelect(beginOffset, curOffset - beginOffset, m_bankId, eventName(status, m_version));
      } else {
        const uint8_t wave = readByte(curOffset++);
        // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named SsdSeqWaveChange sends this slot
        // directly to the SPU2 channel. Effect SWDM banks have no programs, so exported instruments map occupied
        // wave slots into the MIDI program namespace.
        addProgramChange(beginOffset, curOffset - beginOffset, wave, eventName(status, m_version));
      }
      break;
    }
    case 0xac: {
      const uint8_t program = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, program);
      break;
    }
    case 0xaf: {
      if (m_version != 0x0320) {
        const std::string operands = readOperands(count);
        addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                        rawValueDetail(status, operands), VGMItem::Type::Unknown);
        break;
      }
      const uint16_t range = readEventU16LE();
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: This version 0x0320 counterpart to SsdSeqRandomNote replaces two
      // one-byte random-note bounds with one little-endian 16-bit value.
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Packed little-endian range: {}; random-note conversion stub", range),
                      VGMItem::Type::Unknown);
      break;
    }
    case 0xb0:
      addGenericEvent(beginOffset, 1, eventName(status, m_version),
                      "Clears runtime SPU2 envelope overrides; SF2/DLS automation stub", VGMItem::Type::Unknown);
      break;
    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
    case 0xb7:
    case 0xb8:
    case 0xb9:
    case 0xba: {
      const std::string operands = readOperands(count);
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      "Operands: " + operands + "; runtime SPU2 envelope override, SF2/DLS automation stub",
                      VGMItem::Type::Unknown);
      break;
    }
    case 0xbc:
      m_gateTime = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Gate scale: {}/127", m_gateTime), VGMItem::Type::Unknown);
      break;
    case 0xbf: {
      if (PSDSEPS2::isV2(m_version)) {
        addGenericEvent(beginOffset, 1, eventName(status, m_version), rawValueDetail(status, ""),
                        VGMItem::Type::Unknown);
        break;
      }
      const uint8_t value = readByte(curOffset++);
      addSustainEvent(beginOffset, curOffset - beginOffset, value >= 0x40 ? 127 : 0, "Sustain");
      break;
    }
    case 0xbe: {
      const int8_t level = static_cast<int8_t>(readByte(curOffset));
      const std::string operands = readOperands(count);
      const std::string detail = m_version == 0x0300
                                     ? rawValueDetail(status, operands)
                                     : fmt::format("Signed level: {}; updates active constant-envelope LFOs; "
                                                   "synthesis automation stub",
                                                   level);
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version), detail,
                      m_version == 0x0300 ? VGMItem::Type::Unknown : VGMItem::Type::Lfo);
      break;
    }
    case 0xca:
    case 0xcb: {
      if (m_version == 0x0320) {
        const bool enabled = readByte(curOffset++) != 0;
        // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 driver uses CA for SPU2 effect mask 0x8000
        // and CB for reverb-routing mask 0x4000. Both commands carry a boolean rather than a send level.
        addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                        std::string(enabled ? "Enabled" : "Disabled") + "; MIDI send-level stub",
                        VGMItem::Type::Unknown);
        break;
      }
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: These commands route the channel to or from
      // SPU2 reverb and carry no send-level operand.
      addGenericEvent(beginOffset, 1, eventName(status, m_version),
                      status == 0xca ? "SPU2 reverb routing enabled; MIDI send-level stub"
                                     : "SPU2 reverb routing disabled; MIDI send-level stub",
                      VGMItem::Type::Unknown);
      break;
    }
    case 0xd0: {
      const int value = static_cast<int8_t>(readByte(curOffset++));
      addTranspose(beginOffset, curOffset - beginOffset, static_cast<int8_t>(value), eventName(status, m_version));
      break;
    }
    case 0xd1: {
      const int value = std::clamp(static_cast<int>(transpose) + static_cast<int8_t>(readByte(curOffset++)), -128, 127);
      addTranspose(beginOffset, curOffset - beginOffset, static_cast<int8_t>(value), eventName(status, m_version));
      break;
    }
    case 0xd3:
    case 0xd4: {
      const int16_t value = static_cast<int16_t>(readEventU16LE());
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Signed 16-bit value: {}; exact SPU2 pitch automation stub", value),
                      VGMItem::Type::FineTune);
      break;
    }
    case 0xd6: {
      if (m_version != 0x0320) {
        const std::string operands = readOperands(count);
        addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                        rawValueDetail(status, operands), VGMItem::Type::Unknown);
        break;
      }
      const uint8_t value = readByte(curOffset++);
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 handler stores this byte in channel +0x36.
      // The field initializes to zero and is never read by the shipped driver. Its dispatcher position places it
      // in the pitch-control group between Sweep and Vibrate Fade.
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      fmt::format("Write-only channel value: {}", value), VGMItem::Type::FineTune);
      break;
    }
    case 0xdd:
      addPitchBend(beginOffset, 1, 0, "Bender Off");
      break;
    case 0xdf:
      if (PSDSEPS2::isV2(m_version)) {
        addGenericEvent(beginOffset, 1 + count, eventName(status, m_version),
                        rawValueDetail(status, readOperands(count)), VGMItem::Type::Unknown);
      } else {
        addExpression(beginOffset, 2, readByte(curOffset++));
      }
      break;
    case 0xe0:
      addVol(beginOffset, 2, readByte(curOffset++));
      break;
    case 0xe1: {
      const int target = std::clamp(static_cast<int>(vol) + static_cast<int8_t>(readByte(curOffset++)), 0, 127);
      addVol(beginOffset, curOffset - beginOffset, static_cast<uint8_t>(target), "Volume Relative");
      break;
    }
    case 0xe2: {
      const uint16_t duration = readEventU16LE();
      const uint8_t target = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, duration, target, "Volume Fade");
      vol = target;
      break;
    }
    case 0xe8:
      addPan(beginOffset, 2, readByte(curOffset++));
      break;
    case 0xe9: {
      const int target = std::clamp(static_cast<int>(prevPan) + static_cast<int8_t>(readByte(curOffset++)), 0, 127);
      addPan(beginOffset, curOffset - beginOffset, static_cast<uint8_t>(target), "Panpot Relative");
      break;
    }
    case 0xea: {
      const uint16_t duration = readEventU16LE();
      const uint8_t target = readByte(curOffset++);
      addPanSlide(beginOffset, curOffset - beginOffset, duration, target, "Panpot Move");
      prevPan = target;
      break;
    }
    case 0xf0:
    case 0xf1:
    case 0xf2:
    case 0xf6:
    case 0xf7: {
      const std::string operands = readOperands(count);
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      "Operands: " + operands + "; runtime SPU2 LFO routing/automation stub", VGMItem::Type::Lfo);
      break;
    }
    default: {
      const std::string operands = readOperands(count);
      addGenericEvent(beginOffset, curOffset - beginOffset, eventName(status, m_version),
                      rawValueDetail(status, operands), VGMItem::Type::Unknown);
      break;
    }
  }
  return true;
}

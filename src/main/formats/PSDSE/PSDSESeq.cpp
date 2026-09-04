#include "PSDSESeq.h"

#include "PSDSEFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

#include <fmt/format.h>

namespace {
int16_t wrapSigned16(int32_t value) {
  const uint16_t wrapped = static_cast<uint16_t>(value & 0xFFFF);
  return static_cast<int16_t>(wrapped < 0x8000 ? wrapped : static_cast<int32_t>(wrapped) - 0x10000);
}

const char* lfoModeName(uint8_t lfoMode) {
  switch (lfoMode) {
    case 0:
      return "disabled";
    case 1:
      return "fade envelope";
    case 3:
      return "constant envelope";
    default:
      return "unknown";
  }
}

const char* lfoDestinationName(uint8_t lfoDestination) {
  switch (lfoDestination) {
    case 0:
      return "none";
    case 1:
      return "pitch";
    case 2:
      return "volume";
    case 3:
      return "pan";
    default:
      return "unknown";
  }
}

const char* lfoWaveformName(uint8_t lfoWaveform) {
  static constexpr std::array<const char*, 8> names = {
      "half square", "full square", "half triangle", "full triangle", "saw", "reverse saw", "half noise", "full noise",
  };
  return lfoWaveform < names.size() ? names[lfoWaveform] : "invalid";
}
}  // namespace

PSDSESeq::PSDSESeq(RawFile* file, uint32_t offset, uint32_t length, std::string name)
    : VGMSeq(PSDSEFormat::name, file, offset, length, name) {
  m_endianness = PSDSE::magicInfo(file->readWordBE(offset)).endianness;
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setPPQN(48);
  setInitialVolume(127);
}

void PSDSESeq::resetVars() {
  VGMSeq::resetVars();
  if (readMode == READMODE_ADD_TO_UI) {
    m_referencedPatches.clear();
  }
}

void PSDSESeq::addPatchReference(uint16_t bank, uint8_t program) {
  m_referencedPatches.insert((static_cast<uint32_t>(bank) << 8) | program);
}

bool PSDSESeq::referencesPatch(uint32_t bank, uint32_t program) const {
  if (bank > 0xFFFF || program > 0xFF) {
    return false;
  }
  return m_referencedPatches.contains((bank << 8) | program);
}

bool PSDSESeq::parseHeader() {
  uint32_t curOffset = offset();

  const PSDSE::MagicInfo magic = PSDSE::magicInfo(readWordBE(curOffset));
  if (magic.kind != PSDSE::FileKind::Sequence) {
    return false;
  }
  m_endianness = magic.endianness;
  const uint32_t fileLength = PSDSE::readU32(rawFile(), curOffset + 0x08, m_endianness);
  if (fileLength < 0x40 || fileLength > rawFile()->size() - curOffset) {
    return false;
  }
  setLength(fileLength);

  VGMHeader* header = addHeader(curOffset, 0x40, "DSE Sequence Header");
  header->addChild(curOffset, 4, "Magic");
  // [Pokemon Mystery Dungeon: Explorers of Sky]: DseFile_CheckHeader skips +0x04 and +0x10.
  // [Line Attack Heroes]: SsdCheckDataHeader skips +0x04 and +0x10. Audited files store zeroes in both ranges.
  header->addChild(curOffset + 0x04, 4, "Zero Padding");
  header->addChild(curOffset + 0x08, 4, "File Length");
  version = PSDSE::readU16(rawFile(), curOffset + 0x0C, m_endianness);
  header->addChild(curOffset + 0x0C, 2, "Version");
  // [Pokemon Mystery Dungeon: Explorers of Sky]: DseFile_CheckHeader returns +0x0e as the sequence file ID. The
  // seqinfo bank association is independent and routinely differs from this value.
  header->addChild(curOffset + 0x0E, 2, "File ID");
  header->addChild(curOffset + 0x10, 8, "Zero Padding");
  // [Pokemon Mystery Dungeon: Explorers of Sky]: Shipped files and the DSE creation tools identify +0x18 through
  // +0x1f as a creation timestamp.
  header->addChild(curOffset + 0x18, 2, "Creation Year");
  header->addChild(curOffset + 0x1A, 1, "Creation Month");
  header->addChild(curOffset + 0x1B, 1, "Creation Day");
  header->addChild(curOffset + 0x1C, 1, "Creation Hour");
  header->addChild(curOffset + 0x1D, 1, "Creation Minute");
  header->addChild(curOffset + 0x1E, 1, "Creation Second");
  header->addChild(curOffset + 0x1F, 1, "Creation Centisecond");
  char fname[17];
  readBytes(curOffset + 0x20, 16, fname);
  fname[16] = '\0';
  std::string intName = std::string(fname);
  if (!intName.empty()) {
    setName(intName);
  }
  header->addChild(curOffset + 0x20, 16, "File Name");
  // [Pokemon Mystery Dungeon: Explorers of Sky]: DseFile_CheckHeader and the song loader skip these authoring
  // fields. Shipped files consistently store 1, 1, -1, and -1 at these boundaries.
  header->addChild(curOffset + 0x30, 4, "Authoring Metadata 1");
  header->addChild(curOffset + 0x34, 4, "Authoring Metadata 2");
  header->addChild(curOffset + 0x38, 4, "Unused Sentinel 1");
  header->addChild(curOffset + 0x3C, 4, "Unused Sentinel 2");

  // After the header, there should be a "song" chunk. A chunk header is 0x10
  // bytes: label (4), version (4, always 0x01000000 LE), alignment byte (1),
  // 0xFF (1), padding (2), chunk length (4, LE). The song chunk's payload is
  // the 0x30-byte "seqinfo" struct.
  curOffset += 0x40;
  if (readWordBE(curOffset) == 0x736F6E67) {  // "song"
    VGMHeader* songChunk = addHeader(curOffset, 0x10, "Song Chunk");
    songChunk->addChild(curOffset, 4, "Label");
    songChunk->addChild(curOffset + 0x04, 4, "Chunk Version");
    songChunk->addChild(curOffset + 0x08, 1, "Alignment");
    songChunk->addChild(curOffset + 0x09, 1, "0xFF Sentinel");
    songChunk->addChild(curOffset + 0x0A, 2, "Zero Padding");
    songChunk->addChild(curOffset + 0x0C, 4, "Chunk Length");
    curOffset += 0x10;

    // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSequence_LoadSong at ARM9 address 0x0206e554 establishes the
    // following seqinfo field offsets. The structure length depends on the version.
    if (version == 0x0415) {
      VGMHeader* seqInfo = addHeader(curOffset, 0x30, "Seq Info");
      m_defaultBankId = PSDSE::readU16(rawFile(), curOffset, m_endianness);
      seqInfo->addChild(curOffset + 0x00, 2, "Default Bank ID");
      const uint16_t ticksPerQuarter = PSDSE::readU16(rawFile(), curOffset + 0x02, m_endianness);
      seqInfo->addChild(curOffset + 0x02, 2, "Ticks Per Quarter Note");
      // [Pokemon Mystery Dungeon: Explorers of Sky]: The driver divides this field by eight while loading and
      // multiplies it back by eight for sequencer timing.
      if (ticksPerQuarter != 0) {
        setPPQN(ticksPerQuarter);
      }
      // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSequence_LoadSong copies +0x04 into the sequence object,
      // but no subsequent driver routine reads it.
      seqInfo->addChild(curOffset + 0x04, 1, "Unused Sequence Field");
      seqInfo->addChild(curOffset + 0x06, 1, "Num Tracks");
      seqInfo->addChild(curOffset + 0x07, 1, "Num Channels");
      seqInfo->addChild(curOffset + 0x18, 1, "Loop Flag");
      seqInfo->addChild(curOffset + 0x19, 1, "Global Volume Index");
      seqInfo->addChild(curOffset + 0x1A, 1, "Effect ID");
      // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSe_GetBestSeqAllocation compares this byte when choosing
      // which sequence to evict.
      seqInfo->addChild(curOffset + 0x1B, 1, "Sequence Priority");
      curOffset += 0x30;
    } else if (version == 0x0402) {
      addHeader(curOffset, 0x10, "Seq Info");
      curOffset += 0x10;
    }
  }

  return true;
}

bool PSDSESeq::parseTrackPointers() {
  uint32_t curOffset = offset() + 0x40;  // Skip SMDL header
  uint32_t eof = offset() + length();

  while (curOffset < eof) {
    uint32_t chunkType = readWordBE(curOffset);
    if (chunkType == 0x74726B20) {  // "trk "
      uint32_t chunkLen = PSDSE::readU32(rawFile(), curOffset + 0x0C, m_endianness);
      if (chunkLen < 4 || curOffset + 0x10 > eof || chunkLen > eof - (curOffset + 0x10)) {
        return false;
      }

      // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSequence_LoadSong reads the first two data bytes as a track
      // preamble rather than sequence events:
      //   [0x10] track ID, [0x11] channel ID, [0x12]/[0x13] zero padding.
      addTrack<PSDSETrack>(this, curOffset + 0x14, chunkLen - 4, readByte(curOffset + 0x11) & 0x0F);

      // Skip to next chunk, aligned to 4 bytes
      curOffset += 0x10 + chunkLen;
      if ((chunkLen % 4) != 0) {
        curOffset += (4 - (chunkLen % 4));
      }

    } else {
      curOffset += 4;
    }
  }

  return hasTracks();
}

// ************
// PSDSETrack
// ************

PSDSETrack::PSDSETrack(PSDSESeq* parentSeq, long offset, long length, uint8_t channel)
    : SeqTrack(parentSeq, offset, length), m_channel(channel) {
  resetVars();
}

void PSDSETrack::setChannelAndGroupFromTrkNum(int) {
  channel = m_channel;
  channelGroup = 0;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->setChannelGroup(channelGroup);
  }
}

void PSDSETrack::resetVars() {
  SeqTrack::resetVars();
  // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSequence_Reset and DseChannel_Init establish these track and
  // channel reset values.
  currentOctave = 4;
  lastNoteDuration = 0;
  lastWaitDuration = 0;
  noteDurationMultiplier = 127;
  currentBankId = static_cast<PSDSESeq*>(parentSeq)->defaultBankId();
  currentProgram = 0;
  initialBankPending = true;
  vol = 127;
  prevPan = 64;
  dseTuning = 0;
  tuningFadeOffset = 0;
  tieNextNote = false;
  lfos = {};
  selectedLfo = 0;
}

uint16_t PSDSETrack::readEventU16LE() {
  const uint16_t value = readByte(curOffset) | (readByte(curOffset + 1) << 8);
  curOffset += 2;
  return value;
}

int16_t PSDSETrack::readEventS16LE() {
  return wrapSigned16(readEventU16LE());
}

int16_t PSDSETrack::readEventS16BE() {
  const uint16_t value = (readByte(curOffset) << 8) | readByte(curOffset + 1);
  curOffset += 2;
  return wrapSigned16(value);
}

void PSDSETrack::readLfoSetup(LfoSettings& lfo) {
  // [Pokemon Mystery Dungeon: Explorers of Sky]: The retained event handlers read these operands in this order.
  // [Line Attack Heroes]: The retained event handlers read these halfwords as little-endian inside an SMDB file.
  // Multi-byte event operands therefore use command-specific byte order rather than container byte order.
  lfo.amplitude = readEventS16LE();
  lfo.periodMs = readEventU16LE();
  lfo.waveform = readByte(curOffset++);
}

void PSDSETrack::readLfoEnvelope(LfoSettings& lfo) {
  lfo.delayMs = readEventU16LE();
  lfo.fadeMs = readEventU16LE();
}

void PSDSETrack::addLfoEvent(uint32_t offset, uint32_t length, const std::string& name, uint8_t slot,
                             const LfoSettings& lfo) {
  addGenericEvent(
      offset, length, name,
      fmt::format("Slot: {}, mode: {} ({}), destination: {} ({}), waveform: {} ({}), "
                  "amplitude: {}, phase period: {} ms, delay: {} ms, fade: {} ms; "
                  "exact waveform/per-note synthesis automation stub",
                  slot, lfoModeName(lfo.mode), lfo.mode, lfoDestinationName(lfo.destination), lfo.destination,
                  lfoWaveformName(lfo.waveform), lfo.waveform, lfo.amplitude, lfo.periodMs, lfo.delayMs, lfo.fadeMs),
      VGMItem::Type::Lfo);
}

void PSDSETrack::addDseTuningEvent(uint32_t offset, uint32_t length, const std::string& name) {
  const double semitones = dseTuning / 256.0;
  const double coarseSemitones = std::round(semitones);
  const double fineCents = (semitones - coarseSemitones) * 100.0;
  addGenericEvent(offset, length, name,
                  fmt::format("Tuning: {}/256 semitone ({:.6f} cents)", dseTuning, semitones * 100.0),
                  VGMItem::Type::FineTune);
  addCoarseTuningNoItem(coarseSemitones);
  addFineTuningNoItem(fineCents);
}

bool PSDSETrack::readEvent() {
  uint32_t beginOffset = curOffset;
  if (curOffset >= offset() + length()) {
    return false;
  }

  if (initialBankPending) {
    // [Line Attack Heroes]: SsdSequenceLoadSong initializes each channel from seqinfo before its first event.
    // Later SsdSeqBank commands replace this 16-bit SWDL file ID.
    addBankSelectNoItem(currentBankId);
    initialBankPending = false;
  }

  uint8_t status_byte = readByte(curOffset++);

  // Duration table for 0x80-0x8F delay events (48 PPQN)
  static const uint8_t PsdseDurTable[] = {96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};

  if (status_byte <= 0x7F) {
    // Play Note Event
    // 0x00-0x7F: status_byte IS the velocity.
    uint8_t velocity = status_byte;

    // Variable length encoded note flags
    uint8_t noteFlagByte = readByte(curOffset++);
    uint8_t highNybble = (noteFlagByte & 0xF0) >> 4;
    uint8_t lowNybble = (noteFlagByte & 0x0F);

    uint8_t numDurBytes = (highNybble & 0xC) >> 2;

    int8_t octMod = (highNybble & 0x3) - 2;
    currentOctave += octMod;

    uint32_t duration = 0;
    if (numDurBytes > 0) {
      for (int i = 0; i < numDurBytes; i++) {
        duration = (duration << 8) | readByte(curOffset++);
      }
      lastNoteDuration = duration;
    } else {
      duration = lastNoteDuration;
    }

    uint8_t noteIdx = lowNybble;
    uint8_t key = currentOctave * 12 + noteIdx;

    // DSE scales each note's duration by the track's note-duration multiplier
    // (opcode 0xBC, default 127). The engine computes (all 32-bit arithmetic):
    //   scaled = rawDuration * noteDurationMultiplier
    //   hi     = (scaled * 0x02040811) >> 32
    //   ticks  = (hi + ((scaled - hi) >> 1)) >> 6
    // 0x02040811 is the fixed-point reciprocal of 127, so this is
    // (rawDuration * multiplier / 127), which is the identity at the default.
    const uint64_t product = static_cast<uint64_t>(duration) * noteDurationMultiplier;
    const uint32_t scaled = static_cast<uint32_t>(product);
    const uint32_t hi = static_cast<uint32_t>((product * 0x02040811ULL) >> 32);
    const uint32_t noteDuration = (hi + ((scaled - hi) >> 1)) >> 6;

    if (readMode == READMODE_ADD_TO_UI) {
      parentSeq->addBankReference(currentBankId);
      parentSeq->addInstrumentRef(currentProgram);
      // [Line Attack Heroes]: AMBER.SWD mirrors regions between melodic and percussion programs. VGMTrans maps
      // MIDI channel 10 to SoundFont percussion bank 128, so references track the effective exported bank.
      const uint16_t exportBank = channel == 9 ? 128 : currentBankId;
      static_cast<PSDSESeq*>(parentSeq)->addPatchReference(exportBank, currentProgram);
    }
    addNoteByDur(beginOffset, curOffset - beginOffset, key, velocity, noteDuration);
    // [Pokemon Mystery Dungeon: Explorers of Sky]: C0 makes this note update the existing voice's pitch without
    // restarting its sample or envelope.
    // MIDI/SF2 cannot express that per-voice transition reliably, so the note
    // remains retriggered here and only the driver's one-note flag lifetime is modeled.
    tieNextNote = false;
    // Note: Do NOT addTime(duration) here, Wait events handle that

    return true;
  }

  // 0x80-0x8F: Wait Event
  if (status_byte >= 0x80 && status_byte <= 0x8F) {
    uint8_t durIdx = status_byte & 0x0F;
    uint32_t duration = PsdseDurTable[durIdx];
    lastWaitDuration = duration;
    addGenericEvent(beginOffset, curOffset - beginOffset, "Wait", "", VGMItem::Type::Rest);
    addTime(duration);
    return true;
  }

  // [Line Attack Heroes]: RVL_Default.MAP provides the SsdSeq command names. Retail driver handlers establish the
  // command semantics, including build-specific callbacks.
  switch (status_byte) {
    case 0x90:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Last Wait", "", VGMItem::Type::Rest);
      addTime(lastWaitDuration);
      break;
    case 0x91: {
      const int8_t ticks = static_cast<int8_t>(readByte(curOffset++));
      const int64_t adjusted = static_cast<int64_t>(lastWaitDuration) + ticks;
      lastWaitDuration = static_cast<uint32_t>(std::max<int64_t>(0, adjusted));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Last Wait + Ticks", "", VGMItem::Type::Rest);
      addTime(lastWaitDuration);
    } break;
    case 0x92: {
      uint8_t ticks = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait (8-bit)", "", VGMItem::Type::Rest);
      lastWaitDuration = ticks;
      addTime(ticks);
    } break;
    case 0x93: {
      // [Pokemon Mystery Dungeon: Explorers of Sky]: This duration is little-endian.
      // [Line Attack Heroes]: This duration remains little-endian inside a big-endian SMDB container.
      uint16_t ticks = readEventU16LE();
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait (16-bit)", "", VGMItem::Type::Rest);
      lastWaitDuration = ticks;
      addTime(ticks);
    } break;
    case 0x94: {
      uint32_t ticks = readByte(curOffset) | (readByte(curOffset + 1) << 8) | (readByte(curOffset + 2) << 16);
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait (24-bit)", "", VGMItem::Type::Rest);
      lastWaitDuration = ticks;
      addTime(ticks);
    } break;
    case 0x95: {
      // Wait Until Fadeout: pause until all notes have been released, checking
      // every `tickInterval` ticks. The interval byte has no MIDI equivalent.
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait Until Fadeout", "", VGMItem::Type::Rest);
    } break;
    case 0x98:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;
    case 0x99:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Main Loop Begin", "", VGMItem::Type::Marker);
      break;
    case 0x9C: {
      curOffset++;  // sub-loop repeat count
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sub Loop Begin", "", VGMItem::Type::Marker);
    } break;
    case 0x9D: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sub Loop End", "", VGMItem::Type::Marker);
    } break;
    case 0x9E: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sub Loop Break On Last Iteration", "",
                      VGMItem::Type::Marker);
    } break;
    case 0xA0: {
      uint8_t octave = readByte(curOffset++);
      currentOctave = octave;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Octave", "", VGMItem::Type::Unknown);
    } break;
    case 0xA1: {
      int8_t octDiff = readByte(curOffset++);
      currentOctave += octDiff;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Add Octave", "", VGMItem::Type::Unknown);
    } break;
    case 0xA4:  // Tempo Absolute
    case 0xA5: {
      uint8_t bpm = readByte(curOffset++);
      // [Line Attack Heroes]: SsdSeqTempoAbsolute and SsdSeqTempoRelative are byte-identical and both replace the
      // tempo; the 0xA5 operand is not a delta.
      addTempoBPM(beginOffset, curOffset - beginOffset, bpm, status_byte == 0xA4 ? "Tempo Absolute" : "Tempo Relative");
    } break;
    case 0xA8: {
      // [Line Attack Heroes]: SsdSeqBankID reads a 16-bit SWDL file ID in high-byte-first order.
      currentBankId = static_cast<uint16_t>((readByte(curOffset) << 8) | readByte(curOffset + 1));
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank", fmt::format("DSE SWDL ID: {}", currentBankId),
                      VGMItem::Type::Unknown);
      addBankSelectNoItem(currentBankId);
    } break;
    case 0xA9: {
      const uint8_t value = readByte(curOffset++);
      currentBankId = static_cast<uint16_t>((currentBankId & 0x00FF) | (value << 8));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank MSB",
                      fmt::format("DSE SWDL ID: {}", currentBankId), VGMItem::Type::Unknown);
      addBankSelectNoItem(currentBankId);
    } break;
    case 0xAA: {
      const uint8_t value = readByte(curOffset++);
      currentBankId = static_cast<uint16_t>((currentBankId & 0xFF00) | value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank LSB",
                      fmt::format("DSE SWDL ID: {}", currentBankId), VGMItem::Type::Unknown);
      addBankSelectNoItem(currentBankId);
    } break;
    case 0xAB: {
      const uint8_t wave = readByte(curOffset++);
      // [Line Attack Heroes]: RVL_Default.MAP names this handler SsdSeqWaveChange. The retail handler consumes the
      // byte without applying it to channel state.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wave Change",
                      fmt::format("Wave: {}; retail Wii handler is a no-op", wave), VGMItem::Type::Unknown);
    } break;
    case 0xAC:  // Program Change
    {
      uint8_t prog = readByte(curOffset++);
      currentProgram = prog;
      if (readMode == READMODE_ADD_TO_UI) {
        parentSeq->addBankReference(currentBankId);
      }
      addProgramChange(beginOffset, curOffset - beginOffset, prog);
    } break;
    case 0xAF: {
      // [Pokemon Mystery Dungeon: Explorers of Sky]: The driver reads this duration little-endian in milliseconds
      // and converts it to real-time update ticks.
      // [Line Attack Heroes]: The driver uses the same representation, and RVL_Default.MAP names the handler
      // SsdSeqMasterVolumeFader.
      const uint16_t milliseconds = readEventU16LE();
      const uint8_t target = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Master Volume Fade",
                      fmt::format("Target: {}, duration: {} ms; MIDI automation stub", target, milliseconds),
                      VGMItem::Type::Unknown);
    } break;
    // B0-B6 update channel-local envelope overrides. Values above 0x7f are
    // treated as unset by the driver; B4's two 0xff values preserve the
    // corresponding prior fields. A note uses the override structure if any
    // field remains set, otherwise it copies the instrument envelope. The
    // event values are retained here, but runtime envelope automation cannot
    // yet be expressed by the SF2/DLS export APIs.
    case 0xB0:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Restore Envelope Defaults",
                      "Clears all channel envelope overrides; SF2/DLS export stub", VGMItem::Type::Unknown);
      break;
    case 0xB1: {
      const uint8_t value = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Attack Begin",
                      fmt::format("Override: {}; SF2/DLS export stub", value), VGMItem::Type::Unknown);
    } break;
    case 0xB2: {
      const uint8_t value = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Attack Time",
                      fmt::format("Override: {}; SF2/DLS export stub", value), VGMItem::Type::Unknown);
    } break;
    case 0xB3: {
      const uint8_t value = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Hold Time",
                      fmt::format("Override: {}; SF2/DLS export stub", value), VGMItem::Type::Unknown);
    } break;
    case 0xB4: {
      const uint8_t decayTime = readByte(curOffset++);
      const uint8_t sustainLevel = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Decay Time And Sustain Level",
                      fmt::format("Decay: {}, sustain: {}; 0xff preserves that field; SF2/DLS export stub", decayTime,
                                  sustainLevel),
                      VGMItem::Type::Unknown);
    } break;
    case 0xB5: {
      const uint8_t value = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Sustain Time",
                      fmt::format("Override: {}; SF2/DLS export stub", value), VGMItem::Type::Unknown);
    } break;
    case 0xB6: {
      const uint8_t value = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Release Time",
                      fmt::format("Override: {}; SF2/DLS export stub", value), VGMItem::Type::Unknown);
    } break;
    case 0xBC: {
      // Set Note Duration Multiplier: scales subsequent note durations by
      // value/127 (default is 127, i.e. no scaling).
      noteDurationMultiplier = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Note Duration Multiplier", "", VGMItem::Type::Unknown);
    } break;
    case 0xBE: {
      // Force LFO Envelope Level: forces the channel LFO to a constant
      // envelope level (signed byte). No MIDI equivalent.
      const int level = static_cast<int8_t>(readByte(curOffset++));
      addGenericEvent(
          beginOffset, curOffset - beginOffset, "Modulation",
          fmt::format("Signed level: {}; updates active constant-envelope LFOs; synthesis automation stub", level),
          VGMItem::Type::Lfo);
    } break;
    case 0xBF: {
      const uint8_t value = readByte(curOffset++);
      // The driver holds notes for values >= 0x40. Disabling the flag also
      // releases every note currently held by the channel.
      addSustainEvent(beginOffset, curOffset - beginOffset, value >= 0x40 ? 127 : 0, "Sustain");
    } break;
    case 0xC0:
      tieNextNote = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie Next Note",
                      "Reuses the current voice without retriggering its sample or envelope; an "
                      "intervening tuning fade also begins at the current fade offset; MIDI/SF2 "
                      "non-retrigger transition stub",
                      VGMItem::Type::Tie);
      break;
    case 0xC3: {
      const uint8_t value = readByte(curOffset++);
      // [Line Attack Heroes]: RVL_Default.MAP names this handler SsdSeqHeadphoneVolume.
      // [Luminous Arc]: The driver includes this value in note gain only when its global output mode equals 7.
      // [Soma Bringer]: The driver includes this value in note gain only when its global output mode equals 7.
      // [Fushigi no Dungeon: Fuurai no Shiren 4: Kami no Hitomi to Akuma no Heso]: The driver includes this value in
      // note gain only when its global output mode equals 7. SMDL and SMDB do not store that runtime output mode.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Headphone Volume",
                      fmt::format("Value: {}; audible use depends on the game driver's output mode", value),
                      VGMItem::Type::Unknown);
    } break;
    case 0xCB: {
      const uint8_t parameter = readByte(curOffset++);
      const uint8_t value = readByte(curOffset++);
      // [Line Attack Heroes]: RVL_Default.MAP names this handler SsdSeqFXSendLevel.
      // [Luminous Arc]: The driver forwards parameter zero to its effects backend.
      // [Soma Bringer]: The driver forwards parameter zero to its effects backend. Other audited driver builds
      // consume both bytes without updating synthesis state.
      addGenericEvent(
          beginOffset, curOffset - beginOffset, "FX Send Level",
          fmt::format("Parameter: {}; value: {}; backend behavior depends on the game driver", parameter, value),
          VGMItem::Type::Unknown);
    } break;
    case 0xCF: {
      const uint8_t value = readByte(curOffset++);
      // [Fushigi no Dungeon: Fuurai no Shiren DS 2: Sabaku no Majou]: The handler forwards this byte to an empty
      // driver hook.
      // [Gakken: Chuugokugo Zanmai DS]: The handler forwards this byte to an empty driver hook. Other audited
      // drivers mark the opcode invalid, and no audited sequence uses it.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Driver Callback 0xCF",
                      fmt::format("Value: {}; audited callbacks are empty", value), VGMItem::Type::Unknown);
    } break;
    case 0xD0: {
      dseTuning = static_cast<int8_t>(readByte(curOffset++)) * 256;
      addDseTuningEvent(beginOffset, curOffset - beginOffset, "Key Transpose Absolute");
    } break;
    case 0xD1: {
      const int32_t delta = static_cast<int8_t>(readByte(curOffset++)) * 256;
      dseTuning = wrapSigned16(static_cast<int32_t>(dseTuning) + delta);
      addDseTuningEvent(beginOffset, curOffset - beginOffset, "Key Transpose Relative");
    } break;
    case 0xD2: {
      const int32_t delta = static_cast<int8_t>(readByte(curOffset++)) * 4;
      dseTuning = wrapSigned16(static_cast<int32_t>(dseTuning) + delta);
      addDseTuningEvent(beginOffset, curOffset - beginOffset, "Tune");
    } break;
    case 0xD3: {
      // [Line Attack Heroes]: SsdSeqDetune reads this delta little-endian inside a big-endian SMDB container.
      dseTuning = wrapSigned16(static_cast<int32_t>(dseTuning) + readEventS16LE());
      addDseTuningEvent(beginOffset, curOffset - beginOffset, "Detune");
    } break;
    case 0xD4: {
      // [Line Attack Heroes]: SsdSeqSweep reads the duration little-endian and the signed coarse delta from the
      // following byte.
      const uint16_t duration = readEventU16LE();
      const int32_t delta = static_cast<int8_t>(readByte(curOffset++)) * 256;
      const int16_t start = tieNextNote ? tuningFadeOffset : 0;
      tuningFadeOffset = wrapSigned16(static_cast<int32_t>(start) + delta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sweep",
                      fmt::format("Fade offset from {} to {}/256 semitone over {} sequence ticks; MIDI automation stub",
                                  start, tuningFadeOffset, duration),
                      VGMItem::Type::PitchBendSlide);
    } break;
    case 0xD5: {
      const uint8_t first = readByte(curOffset++);
      const uint8_t second = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Random Key",
                      fmt::format("Inclusive note range: {}..{}; nondeterministic playback stub",
                                  std::min(first, second), std::max(first, second)),
                      VGMItem::Type::Unknown);
    } break;
    case 0xD6: {
      const uint16_t amplitude = readEventU16LE();
      addGenericEvent(beginOffset, curOffset - beginOffset, "Random Note",
                      fmt::format("Amplitude: {}/256 semitone; nondeterministic playback stub", amplitude),
                      VGMItem::Type::FineTune);
    } break;
    case 0xD8: {
      const uint16_t value = static_cast<uint16_t>((readByte(curOffset) << 8) | readByte(curOffset + 1));
      curOffset += 2;
      // [Line Attack Heroes]: RVL_Default.MAP names this handler SsdSeqPortamento, and the retail handler reads the
      // operand big-endian, unlike the adjacent detune and sweep operands.
      // [Pokemon Mystery Dungeon: Explorers of Sky]: The version 0x0415 driver also reads this operand big-endian.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento",
                      fmt::format("Big-endian value: {}; synthesis automation stub", value), VGMItem::Type::Unknown);
    } break;

    case 0xD7: {
      // [Pokemon Mystery Dungeon: Explorers of Sky]: SsdSeqBender reads a high-byte-first signed pitch-wheel value.
      // [Line Attack Heroes]: SsdSeqBender uses the same byte order despite adjacent little-endian operands.
      const int16_t bend = readEventS16BE();
      addPitchBend(beginOffset, curOffset - beginOffset, std::clamp<int16_t>(bend, -8192, 8191), "Bender");
    } break;

    case 0xDB: {
      const uint8_t semitones = readByte(curOffset++);
      if (semitones == 0) {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Bend Range", "Use instrument bend range",
                        VGMItem::Type::FineTune);
      } else {
        addPitchBendRange(beginOffset, curOffset - beginOffset, semitones * 100, "Bend Range");
      }
    } break;
    case 0xDC:  // Setup Key Bend LFO (pitch)
    case 0xE4:  // Setup Volume LFO
    case 0xEC:  // Setup Pan LFO
    {
      const uint8_t slot = status_byte == 0xDC ? 0 : status_byte == 0xE4 ? 1 : 2;
      LfoSettings& lfo = lfos[slot];
      lfo.mode = 1;
      lfo.destination = slot + 1;
      readLfoSetup(lfo);
      addLfoEvent(beginOffset, curOffset - beginOffset,
                  slot == 0   ? "Vibrate Parameter"
                  : slot == 1 ? "Tremolo Parameter"
                              : "Shake Parameter",
                  slot, lfo);
    } break;
    case 0xDD:  // Setup Key Bend LFO Envelope
    case 0xE5:  // Setup Volume LFO Envelope
    case 0xED:  // Setup Pan LFO Envelope
    {
      const uint8_t slot = status_byte == 0xDD ? 0 : status_byte == 0xE5 ? 1 : 2;
      LfoSettings& lfo = lfos[slot];
      readLfoEnvelope(lfo);
      addLfoEvent(beginOffset, curOffset - beginOffset,
                  slot == 0   ? "Vibrate Delay"
                  : slot == 1 ? "Tremolo Delay"
                              : "Shake Delay",
                  slot, lfo);
    } break;
    case 0xDF:  // Use Key Bend LFO
    case 0xE7:  // Use Volume LFO
    case 0xEF:  // Use Pan LFO
    {
      const uint8_t slot = status_byte == 0xDF ? 0 : status_byte == 0xE7 ? 1 : 2;
      const uint8_t rawMode = readByte(curOffset++);
      LfoSettings& lfo = lfos[slot];
      lfo.mode = rawMode == 2 ? 1 : rawMode;
      lfo.destination = lfo.mode == 0 ? 0 : slot + 1;
      addLfoEvent(beginOffset, curOffset - beginOffset,
                  slot == 0   ? "Vibrate Mode"
                  : slot == 1 ? "Tremolo Mode"
                              : "Shake Mode",
                  slot, lfo);
    } break;
    case 0xE0:  // Volume
    {
      const int volume = static_cast<int8_t>(readByte(curOffset++));
      addVol(beginOffset, curOffset - beginOffset, static_cast<uint8_t>(std::clamp(volume, 0, 127)));
    } break;
    case 0xE1:  // Volume Delta
    {
      const int delta = static_cast<int8_t>(readByte(curOffset++));
      const uint8_t target = static_cast<uint8_t>(std::clamp(static_cast<int>(vol) + delta, 0, 127));
      addVol(beginOffset, curOffset - beginOffset, target, "Volume Delta");
    } break;
    case 0xE9:  // Pan Delta
    {
      const int delta = static_cast<int8_t>(readByte(curOffset++));
      const uint8_t target = static_cast<uint8_t>(std::clamp(static_cast<int>(prevPan) + delta, 0, 127));
      addPan(beginOffset, curOffset - beginOffset, target, "Pan Delta");
    } break;
    case 0xE2:  // Volume Fade
    {
      // [Line Attack Heroes]: SsdSeqVolumeFade reads this duration little-endian inside a big-endian SMDB file.
      const uint16_t duration = readEventU16LE();
      const int targetRaw = static_cast<int8_t>(readByte(curOffset++));
      const uint8_t target = static_cast<uint8_t>(std::clamp(targetRaw, 0, 127));
      if (duration == 0) {
        addVol(beginOffset, curOffset - beginOffset, target, "Volume Fade (Immediate)");
      } else {
        addVolSlide(beginOffset, curOffset - beginOffset, duration, target, "Volume Fade");
        vol = target;
      }
    } break;
    case 0xEA:  // Pan Fade
    {
      // [Line Attack Heroes]: SsdSeqPanpotMove reads this duration little-endian inside an SMDB file.
      const uint16_t duration = readEventU16LE();
      const int targetRaw = static_cast<int8_t>(readByte(curOffset++));
      const uint8_t target = static_cast<uint8_t>(std::clamp(targetRaw, 0, 127));
      if (duration == 0) {
        addPan(beginOffset, curOffset - beginOffset, target, "Pan Fade (Immediate)");
      } else {
        addPanSlide(beginOffset, curOffset - beginOffset, duration, target, "Pan Fade");
        prevPan = target;
      }
    } break;
    case 0xE3:  // Expression
    {
      uint8_t expr = readByte(curOffset++);
      addExpression(beginOffset, curOffset - beginOffset, expr);
    } break;
    case 0xE8:  // Pan
    {
      uint8_t pan = readByte(curOffset++);
      addPan(beginOffset, curOffset - beginOffset, pan);
    } break;
    case 0xF0:  // Setup LFO (generic, selected by channel lfo index)
    {
      LfoSettings& lfo = lfos[selectedLfo];
      readLfoSetup(lfo);
      addLfoEvent(beginOffset, curOffset - beginOffset, "LFO Parameter", selectedLfo, lfo);
    } break;
    case 0xF1:  // Setup LFO Envelope
    {
      LfoSettings& lfo = lfos[selectedLfo];
      readLfoEnvelope(lfo);
      addLfoEvent(beginOffset, curOffset - beginOffset, "LFO Delay", selectedLfo, lfo);
    } break;
    case 0xF2:  // Set LFO Parameter
    {
      const uint8_t parameter = readByte(curOffset++);
      const uint8_t value = readByte(curOffset++);
      std::string detail = fmt::format("Parameter: {}, value: {}", parameter, value);

      if (parameter == 1) {
        if (value < lfos.size()) {
          selectedLfo = value;
          fmt::format_to(std::back_inserter(detail), "; selected slot {}", selectedLfo);
        } else {
          detail += "; invalid LFO slot, selection unchanged";
        }
      } else {
        LfoSettings& lfo = lfos[selectedLfo];
        switch (parameter) {
          case 0:
            detail += "; driver no-op";
            break;
          case 2:
            lfo.mode = value;
            break;
          case 3:
            lfo.destination = value;
            break;
          case 4:
            lfo.waveform = value;
            break;
          case 5:
            switch (lfo.destination) {
              case 1:
                lfo.amplitude = value * 10;
                break;
              case 2:
                lfo.amplitude = -static_cast<int32_t>(value) * 20;
                break;
              case 3:
                lfo.amplitude = value * 20;
                break;
              case 4:
                lfo.amplitude = value * 10;
                break;
              default:
                lfo.amplitude = value * 20;
                break;
            }
            break;
          case 6:
            lfo.periodMs = value * 5;
            break;
          case 7:
            lfo.delayMs = value * 20;
            break;
          case 8:
            lfo.delayMs = static_cast<uint16_t>((lfo.delayMs & 0xFF00) | value);
            break;
          case 9:
            lfo.delayMs = static_cast<uint16_t>((lfo.delayMs & 0x00FF) | (value << 8));
            break;
          case 10:
            // [Pokemon Mystery Dungeon: Explorers of Sky]: The driver multiplies this byte by 20 before storing the
            // LFO fade time in milliseconds.
            // [Fushigi no Dungeon: Fuurai no Shiren 4: Kami no Hitomi to Akuma no Heso]: The driver uses the same
            // conversion, and the audited corpus uses value 60.
            // [Fushigi no Dungeon: Fuurai no Shiren 5: Fortune Tower to Unmei no Dice]: The audited corpus also uses
            // value 60.
            lfo.fadeMs = value * 20;
            break;
          default:
            detail += "; unknown parameter";
            break;
        }
      }

      if (parameter >= 2 && parameter <= 10) {
        const LfoSettings& lfo = lfos[selectedLfo];
        fmt::format_to(std::back_inserter(detail),
                       "; selected slot: {}, mode: {}, destination: {}, waveform: {}, amplitude: {}, "
                       "phase period: {} ms, delay: {} ms, fade: {} ms",
                       selectedLfo, lfo.mode, lfo.destination, lfo.waveform, lfo.amplitude, lfo.periodMs, lfo.delayMs,
                       lfo.fadeMs);
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Data", detail, VGMItem::Type::Lfo);
    } break;
    case 0xF3:  // Use LFO
    {
      const uint8_t slot = readByte(curOffset++);
      const uint8_t rawMode = readByte(curOffset++);
      const uint8_t destination = readByte(curOffset++);
      if (slot < lfos.size()) {
        selectedLfo = slot;
        LfoSettings& lfo = lfos[slot];
        lfo.mode = rawMode == 2 ? 1 : rawMode;
        lfo.destination = destination;
        addLfoEvent(beginOffset, curOffset - beginOffset, "LFO Mode", slot, lfo);
      } else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Mode",
                        fmt::format("Invalid slot: {}, mode: {}, destination: {}", slot, rawMode, destination),
                        VGMItem::Type::Lfo);
      }
    } break;

    case 0xF6:  // Cue Point
    {
      const uint8_t signalId = readByte(curOffset++);
      // [Line Attack Heroes]: RVL_Default.MAP names this handler SsdSeqCuePoint.
      // [Pokemon Mystery Dungeon: Explorers of Sky]: The handler stores the byte in sequence state and invokes the
      // registered game callback with event type 8.
      // [Fushigi no Dungeon: Fuurai no Shiren 4: Kami no Hitomi to Akuma no Heso]: The handler performs the same
      // state update and callback.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Cue Point",
                      fmt::format("Cue ID: {}; invokes the registered game callback", signalId),
                      VGMItem::Type::Unknown);
    } break;

    case 0xF8:  // Channel Control
    {
      const uint8_t parameter = readByte(curOffset++);
      const uint8_t value = readByte(curOffset++);
      // [Line Attack Heroes]: RVL_Default.MAP names this handler SsdSeqChannelControl, and the retail handler skips
      // both bytes without updating channel state.
      // [Pokemon Mystery Dungeon: Explorers of Sky]: The handler skips both bytes without updating channel state.
      // [Fushigi no Dungeon: Fuurai no Shiren 4: Kami no Hitomi to Akuma no Heso]: The handler skips both bytes
      // without updating channel state.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Channel Control",
                      fmt::format("Parameter: {}; value: {}; audited handlers are no-ops", parameter, value),
                      VGMItem::Type::Unknown);
    } break;
    default: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Invalid Opcode", "", VGMItem::Type::Unknown);
    } break;
  }

  return true;
}

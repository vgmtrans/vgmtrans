/*
* VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiVendettaInstr.h"
#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2Format.h"
#include "KonamiAdpcm.h"
#include "LogManager.h"
#include "VGMRgn.h"

KonamiVendettaSampleInstrSet::KonamiVendettaSampleInstrSet(
  RawFile *file,
  u32 offset,
  u32 instrTableOffsetYM2151,
  u32 instrTableOffsetK053260,
  u32 sampInfosOffset,
  u32 drumBanksOffset,
  u32 drumsOffset,
  vendetta_sub_offsets subOffsets,
  const std::vector<konami_vendetta_instr_k053260>& instrs,
  const std::vector<konami_vendetta_sample_info>& sampInfos,
  std::string name,
  KonamiTMNT2FormatVer fmtVer
  ) : VGMInstrSet(KonamiTMNT2Format::name, file, offset, 0, std::move(name)),
      m_instrTableOffsetYM2151(instrTableOffsetYM2151),
      m_instrTableOffsetK053260(instrTableOffsetK053260),
      m_sampInfosOffset(sampInfosOffset),
      m_drumBanksOffset(drumBanksOffset),
      m_drumsOffset(drumsOffset),
      m_subOffsets(subOffsets),
      m_instrsK053260(instrs),
      m_sampInfos(sampInfos),
      m_fmtVer(fmtVer)
{
}

void KonamiVendettaSampleInstrSet::addInstrInfoChildren(VGMItem* instrInfoItem, u32 off) {
  std::string sampleTypeStr;
  u8 flagsByte = readByte(off);
  sampleTypeStr = (flagsByte & 0x10) ? "KADPCM" : "PCM 8";

  if (flagsByte & 0x08) {
    sampleTypeStr += " (Reverse)";
  }
  instrInfoItem->addChild(off + 0, 1, fmt::format("Flags - Sample Type: {}", sampleTypeStr));

  instrInfoItem->addChild(off + 1, 2, "Sample Length");
  instrInfoItem->addChild(off + 3, 3, "Sample Offset");
  instrInfoItem->addChild(off + 6, 1, "Volume");
  instrInfoItem->addChild(off + 7, 1, "Note Duration (fractional)");
  instrInfoItem->addChild(off + 8, 1, "Release Duration / Rate?");
  instrInfoItem->addChild(off + 9, 1, "Pan");
}

void KonamiVendettaSampleInstrSet::addSampleInfoItems() {
  size_t numSampInfos = m_sampInfos.size();
  auto sampInfosItem = addChild(
    m_sampInfosOffset,
    numSampInfos * sizeof(konami_vendetta_sample_info),
    "Sample Infos"
  );
  for (int i = 0; i < numSampInfos; ++i) {
    u32 offset = m_sampInfosOffset + i * sizeof(konami_vendetta_sample_info);
    auto sampInfo = sampInfosItem->addChild(
      offset,
      sizeof(konami_vendetta_sample_info),
      fmt::format("Sample Info {}", i)
    );
    sampInfo->addChild(offset, 2, "Pitch");
    sampInfo->addChild(offset+2, 2, "Length");
    sampInfo->addChild(offset+4, 3, "Offset");
    sampInfo->addChild(offset+7, 1, "ADPCM Flag");
  }
}

bool KonamiVendettaSampleInstrSet::parseInstrPointers() {
  disableAutoAddInstrumentsAsChildren();

  addSampleInfoItems();

  if (!parseMelodicInstrs())
    return false;
  if (!parseDrums())
    return false;

  return true;
}

bool KonamiVendettaSampleInstrSet::parseMelodicInstrs() {
  auto instrTableItem = addChild(m_instrTableOffsetK053260, m_instrsK053260.size() * 2, "Instrument Table");
  u16 minInstrOffset = -1;
  u16 maxInstrOffset = 0;
  for (int i = 0; i < m_instrsK053260.size(); ++i) {
    minInstrOffset = std::min(minInstrOffset, readShort(m_instrTableOffsetK053260 + i * 2));
    maxInstrOffset = std::max(maxInstrOffset, readShort(m_instrTableOffsetK053260 + i * 2));
    instrTableItem->addChild(m_instrTableOffsetK053260 + (i * 2), 2, fmt::format("Instrument {} Pointer", i));
  }

  auto instrsItem = addChild(
    minInstrOffset,
    (maxInstrOffset + sizeof(konami_vendetta_instr_k053260)) - minInstrOffset,
    "Instruments"
  );
  int instrNum = 0;
  for (auto& instrInfo : m_instrsK053260) {
    u32 offset = readShort(m_instrTableOffsetK053260 + instrNum * 2);

    // Add the UI Items
    auto instrItem = instrsItem->addChild(offset, 3, fmt::format("Instrument {}", instrNum));
    instrItem->addChild(offset, 1, "Sample Info Index");
    instrItem->addChild(offset + 1, 1, "Attenuation");
    instrItem->addUnknownChild(offset + 2, 1);

    if (instrInfo.samp_info_idx >= m_sampInfos.size()) {
      instrNum++;
      L_WARN("Invalid sample info index: {:d}", instrInfo.samp_info_idx);
      continue;
    }
    auto sampInfo = m_sampInfos[instrInfo.samp_info_idx];

    std::string name = fmt::format("Instrument {}", instrNum);
    VGMInstr* instr = new VGMInstr(
      this,
      offset,
      sizeof(konami_vendetta_instr_k053260),
      instrNum / 128,
      instrNum % 128,
      name
    );

    VGMRgn* rgn = new VGMRgn(instr, offset, sizeof(konami_vendetta_instr_k053260));
    rgn->sampOffset = sampInfo.start();
    rgn->sampDataLength = sampInfo.length();
    instr->addRgn(rgn);
    aInstrs.push_back(instr);
    instrNum += 1;
  }
  return true;
}

bool KonamiVendettaSampleInstrSet::parseDrums() {

  std::vector<konami_vendetta_drum_info> drumInfos;

  // Load Drums. Drums end at YM2151 Instr Table
  VGMItem* drumsItem = new VGMItem(nullptr, m_drumsOffset, 0, "Drums");

  std::map<u16, int> drumOffsetToIdx;
  int drumIdx = 0;
  for (u16 i = m_drumsOffset; i < m_instrTableOffsetYM2151; ) {
    VGMItem* drumItem = drumsItem->addChild(i, 0, fmt::format("Drum {}", drumIdx));
    auto instrData = drumItem->addChild(i, 3, "Instrument Data");
    instrData->addChild(i, 1, "Sample Info Index");
    instrData->addChild(i + 1, 1, "Attenuation");
    instrData->addUnknownChild(i + 2, 1);
    // track the drum's code offset (+3) to its index as this is how each drum in a bank is referenced
    drumOffsetToIdx[i + 3] = drumIdx++;
    auto drumInfo = parseVendettaDrum(rawFile(), i, m_subOffsets, drumItem);
    drumInfos.emplace_back(drumInfo);
  }
  auto lastDrum = drumsItem->children().back();
  drumsItem->unLength = (lastDrum->dwOffset + lastDrum->unLength) - m_drumsOffset;

  if (drumInfos.size() == 0)
    return false;
  u16 drumsOffset = drumOffsetToIdx.begin()->first - 3;
  int numDrumTables = (drumsOffset - m_drumBanksOffset) / 0x20;

  auto drumBanksItem = addChild(m_drumBanksOffset, numDrumTables * 0x20, "Drum Banks");

  VGMInstr* drumKit = new VGMInstr(this, dwOffset, unLength, 1, 0, "Drum Kit");

  int drumNum = 0;
  for (u32 i = 0; i < numDrumTables; ++i) {
    u16 drumBankPtr = m_drumBanksOffset + (i * 0x20);
    auto drumBankItem = drumBanksItem->addChild(drumBankPtr, 0x20, fmt::format("Drum Bank {}", i));
    for (int j = 0; j < 16; ++j) {
      u32 ptrOffset = drumBankPtr + j * 2;
      u16 ptr = readShort(ptrOffset);

      if (ptr == 0)
        continue;

      drumBankItem->addChild(ptrOffset, 2, "Drum Pointer");

      int drumIdx = drumOffsetToIdx[ptr];
      const konami_vendetta_drum_info& drumInfo = drumInfos[drumIdx];
      m_drumKeyMap[(i * 16) + j] = drumInfo;

      VGMRgn* rgn = new VGMRgn(drumKit, ptr - 3, 3);
      auto sampInfoIdx = drumInfo.instr.samp_info_idx;
      auto sampInfo = m_sampInfos[sampInfoIdx];
      rgn->sampOffset = sampInfo.start();
      rgn->sampDataLength = sampInfo.length();
      u8 key = (i * 16) + j;
      rgn->keyLow = key;
      rgn->keyHigh = key;
      rgn->unityKey = key;

      s16 pitch = drumInfo.pitch != -1 ? drumInfo.pitch : (sampInfo.pitch_hi << 8) + sampInfo.pitch_lo;
      double relativePitchCents = k053260_pitch_cents(pitch);
      rgn->coarseTune = relativePitchCents / 100;
      rgn->fineTune = static_cast<int>(relativePitchCents) % 100;

      drumKit->addRgn(rgn);
      drumNum += 1;
    }
  }
  aInstrs.emplace_back(drumKit);
  return true;
}

konami_vendetta_drum_info KonamiVendettaSampleInstrSet::parseVendettaDrum(
  RawFile* programRom,
  u16& offset,
  const vendetta_sub_offsets& subOffsets,
  VGMItem* drumItem
) {
  // Each drum is defined by a 3 byte instrument data block, followed by actual Z80 instructions
  // We will store the meaning of those instructions in a konami_vendetta_drum_info instance.
  konami_vendetta_drum_info drumInfo;
  offset += sizeof(konami_vendetta_instr_k053260);
  u16 hl = 0;
  u8 a = 0;
  while (offset < programRom->size()) {
    u8 opcode = programRom->readByte(offset);
    switch (opcode) {
      case 0x21:    // LD HL <data> - loads the instr table offset or the pitch
        hl = programRom->readShort(offset + 1);
        drumItem->addChild(offset, 3, fmt::format("Instruction - LD HL {:02X}", hl));
        offset += 3;
        break;

      case 0x3E:    // LD A <data> - loads the pan value
        a = programRom->readByte(offset + 1);
        drumItem->addChild(offset, 2, fmt::format("Instruction - LD A {:X}", a));
        offset += 2;
        break;

      case 0xCD: {
        // CALL <addr> - calls a subroutine
        u16 dest = programRom->readShort(offset + 1);
        if (dest == subOffsets.load_instr) {
          programRom->readBytes(hl, sizeof(konami_vendetta_instr_k053260), &drumInfo.instr);
          drumItem->addChild(offset, 3, fmt::format("Instruction - CALL load_instrument"));
        } else if (dest == subOffsets.set_pan) {
          drumInfo.pan = a;
          drumItem->addChild(offset, 3, fmt::format("Instruction - CALL set_pan"));
        } else if (dest == subOffsets.set_pitch) {
          drumInfo.pitch = hl;
          drumItem->addChild(offset, 3, fmt::format("Instruction - CALL set_pitch"));
        } else if (dest == subOffsets.note_on) {
          drumItem->addChild(offset, 3, fmt::format("Instruction - CALL note_on"));
        }
        offset += 3;
        break;
      }

      case 0xC3: {
        // JP - sets the drum ptr to state and we're done
        u16 dest = programRom->readShort(offset + 1);
        drumItem->addChild(offset, 3, fmt::format("Instruction - JP {:02X}", dest));
        offset += 3;
        drumItem->unLength = offset - drumItem->dwOffset;
        return drumInfo;
      }
      default: {
        L_WARN("Unknown opcode {:02X} in KonamiTMNT2 drum table parse", opcode);
        offset += 1;
        break;
      }
    }
  }
  drumItem->unLength = offset - drumItem->dwOffset;
  return drumInfo;
}
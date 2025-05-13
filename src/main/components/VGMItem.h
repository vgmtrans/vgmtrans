#pragma once

#include "common.h"
#include <algorithm>
#include <vector>
#include <ranges>

template <class T>
class Menu;
class RawFile;
class VGMFile;
class VGMHeader;

class VGMItem {
public:
  enum Icon {
    ICON_SEQ,
    ICON_INSTRSET,
    ICON_SAMPCOLL,
    ICON_UNKNOWN,
    ICON_NOTE,
    ICON_NOTE_OFF,
    ICON_TRACK,
    ICON_REST,
    ICON_CONTROL,
    ICON_STARTREP,
    ICON_ENDREP,
    ICON_TIMESIG,
    ICON_TEMPO,
    ICON_PROGCHANGE,
    ICON_TRACKEND,
    ICON_COLL,
    ICON_INSTR,
    ICON_SAMP,
    ICON_BINARY,
    ICON_LOOP,
    ICON_LOOP_FOREVER,
    ICON_VOLUME,
    ICON_PAN,
    ICON_ADSR,
    ICON_PITCHBEND,
    ICON_JUMP,
  };
  enum class Type {
    Unknown,
    Unrecognized,
    Header,
    Misc,
    Marker,
    TimeSignature,
    Tempo,
    ProgramChange,
    BankSelect,
    Transpose,
    Priority,
    MasterVolume,
    Volume,
    Expression,
    Pan,
    NoteOn,
    NoteOff,
    DurationNote,
    Tie,
    Rest,
    PitchBend,
    PitchBendRange,
    Modulation,
    Portamento,
    PortamentoTime,
    ChangeState,
    Adsr,
    Lfo,
    Reverb,
    Sustain,
    Loop,
    LoopForever,
    TrackEnd,
  };

public:
  VGMItem();
  VGMItem(VGMFile *vgmfile,
          uint32_t offset,
          uint32_t length = 0,
          std::string name = "",
          Type type = Type::Unknown);
  virtual ~VGMItem();

  friend bool operator>(VGMItem &item1, VGMItem &item2);
  friend bool operator<=(VGMItem &item1, VGMItem &item2);
  friend bool operator<(VGMItem &item1, VGMItem &item2);
  friend bool operator>=(VGMItem &item1, VGMItem &item2);

  [[nodiscard]] const std::string& name() const noexcept { return m_name; }
  void setName(const std::string& newName) { m_name = newName; }

  [[nodiscard]] VGMFile* vgmFile() const { return m_vgmfile; }
  [[nodiscard]] RawFile* rawFile() const;

  virtual bool isItemAtOffset(uint32_t offset, bool matchStartOffset = false);
  VGMItem* getItemAtOffset(uint32_t offset, bool matchStartOffset = false);

  virtual uint32_t guessLength();
  virtual void setGuessedLength();
  virtual std::string description() { return ""; }
  virtual Icon icon() { return ICON_BINARY; }
  virtual void addToUI(VGMItem *parent, void *UI_specific);

  const std::vector<VGMItem*>& children() { return m_children; }
  VGMItem* addChild(VGMItem* child);
  VGMItem* addChild(uint32_t offset, uint32_t length, const std::string &name);
  VGMItem* addUnknownChild(uint32_t offset, uint32_t length);
  VGMHeader* addHeader(uint32_t offset, uint32_t length, const std::string &name = "Header");
  void transferChildren(VGMItem* destination);

  template <std::ranges::input_range Range>
  requires std::convertible_to<std::ranges::range_value_t<Range>, VGMItem*>
  void addChildren(const Range& items) {
    std::ranges::copy(items, std::back_inserter(m_children));
  }

  void sortChildrenByOffset();

protected:
  uint32_t readBytes(uint32_t index, uint32_t count, void *buffer) const;
  [[nodiscard]] uint8_t readByte(uint32_t offset) const;
  [[nodiscard]] uint16_t readShort(uint32_t offset) const;
  [[nodiscard]] uint32_t getWord(uint32_t offset) const;
  [[nodiscard]] uint16_t getShortBE(uint32_t offset) const;
  [[nodiscard]] uint32_t getWordBE(uint32_t offset) const;
  bool isValidOffset(uint32_t offset) const;
  // FIXME: clearChildren() is a workaround for VGMSeqNoTrks' multiple inheritance diamond problem

public:
  uint32_t dwOffset;  // offset in the pDoc data buffer
  uint32_t unLength;  // num of bytes the event engulfs
  Type type;

private:
  std::vector<VGMItem *> m_children;
  VGMFile *m_vgmfile;
  std::string m_name;
};

struct ItemPtrOffsetCmp {
  bool operator()(const VGMItem *a, const VGMItem *b) const { return (a->dwOffset < b->dwOffset); }
};

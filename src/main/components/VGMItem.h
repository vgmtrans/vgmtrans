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
  enum class Type {
    Adsr,
    BankSelect,
    ChangeState,
    Control,
    UseDrumKit,
    DurationChange,
    DurationNote,
    Expression,
    ExpressionSlide,
    FineTune,
    Header,
    Instrument,
    Jump,
    JumpConditional,
    Loop,
    LoopBreak,
    LoopForever,
    Lfo,
    Marker,
    MasterVolume,
    MasterVolumeSlide,
    Misc,
    Modulation,
    Mute,
    Noise,
    Nop,
    NoteOff,
    NoteOn,
    Octave,
    Pan,
    PanLfo,
    PanSlide,
    PanEnvelope,
    PitchBend,
    PitchBendSlide,
    PitchBendRange,
    PitchEnvelope,
    Portamento,
    PortamentoTime,
    Priority,
    ProgramChange,
    RepeatStart,
    RepeatEnd,
    Rest,
    Reverb,
    Sample,
    Sustain,
    Tempo,
    Tie,
    TimeSignature,
    Track,
    TrackEnd,
    Transpose,
    Tremelo,
    Unrecognized,
    Unknown,
    Vibrato,
    Volume,
    VolumeSlide,
    VolumeEnvelope,
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
  virtual void addToUI(VGMItem *parent, void *UI_specific);

  const std::vector<VGMItem*>& children() { return m_children; }
  [[nodiscard]] uint32_t offset() const noexcept { return m_offset; }
  [[nodiscard]] uint32_t length() const noexcept { return m_length; }
  void setOffset(uint32_t offset);
  void setLength(uint32_t length);
  void setRange(uint32_t offset, uint32_t length);
  VGMItem* addChild(VGMItem* child);
  VGMItem* addChild(uint32_t offset, uint32_t length, const std::string &name);
  VGMItem* addUnknownChild(uint32_t offset, uint32_t length);
  VGMHeader* addHeader(uint32_t offset, uint32_t length, const std::string &name = "Header");
  void removeChildren();
  void transferChildren(VGMItem* destination);

  template <std::ranges::input_range Range>
  requires std::convertible_to<std::ranges::range_value_t<Range>, VGMItem*>
  void addChildren(const Range& items) {
    std::ranges::copy(items, std::back_inserter(m_children));
    for (auto *child : m_children) {
      child->m_parent = this;
    }
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
  const Type type;

private:
  std::vector<VGMItem *> m_children;
  VGMFile *m_vgmfile;
  VGMItem *m_parent = nullptr;
  uint32_t m_offset;  // offset in the pDoc data buffer
  uint32_t m_length;  // num of bytes the event engulfs
  std::string m_name;
};

struct ItemPtrOffsetCmp {
  bool operator()(const VGMItem *a, const VGMItem *b) const { return (a->offset() < b->offset()); }
};

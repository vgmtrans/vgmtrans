#pragma once

#include "base/Types.h"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
    ChannelPressure,
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
          u32 offset,
          u32 length = 0,
          std::string name = "",
          Type type = Type::Unknown);
  VGMItem(const VGMItem& other) = delete;
  VGMItem(VGMItem&& other) noexcept;
  VGMItem& operator=(const VGMItem& other) = delete;
  VGMItem& operator=(VGMItem&& other) noexcept = delete;
  virtual ~VGMItem();

  friend bool operator>(VGMItem &item1, VGMItem &item2);
  friend bool operator<=(VGMItem &item1, VGMItem &item2);
  friend bool operator<(VGMItem &item1, VGMItem &item2);
  friend bool operator>=(VGMItem &item1, VGMItem &item2);

  [[nodiscard]] const std::string& name() const noexcept { return m_name; }
  void setName(const std::string& newName) { m_name = newName; }

  [[nodiscard]] VGMFile* vgmFile() const { return m_vgmfile; }
  [[nodiscard]] RawFile* rawFile() const;

  virtual bool isItemAtOffset(u32 offset, bool matchStartOffset = false);
  VGMItem* getItemAtOffset(u32 offset, bool matchStartOffset = false);

  virtual u32 guessLength() const;
  virtual void setGuessedLength();
  virtual std::string description() { return ""; }
  virtual void addToUI(VGMItem *parent, void *UI_specific);

  [[nodiscard]] std::span<VGMItem* const> children() const { return m_children; }
  [[nodiscard]] u32 offset() const noexcept { return m_offset; }
  [[nodiscard]] u32 length() const noexcept { return m_length; }
  void setOffset(u32 offset);
  void setLength(u32 length);
  void setRange(u32 offset, u32 length);
  VGMItem* sinkChild(std::unique_ptr<VGMItem>&& child);
  template <class ChildType, class... Args>
  ChildType* addChild(Args&&... args) {
    auto child = std::make_unique<ChildType>(std::forward<Args>(args)...);
    auto* rawChild = child.get();
    sinkChild(std::move(child));
    return rawChild;
  }
  VGMItem* addChild(u32 offset, u32 length, const std::string &name);
  VGMItem* addUnknownChild(u32 offset, u32 length);
  VGMHeader* addHeader(u32 offset, u32 length, const std::string &name = "Header");
  void removeChildren();
  void transferChildren(VGMItem* destination);

  void sortChildrenByOffset();

protected:
  u32 readBytes(u32 index, u32 count, void *buffer) const;
  [[nodiscard]] u8 readByte(u32 offset) const;
  [[nodiscard]] u16 readShort(u32 offset) const;
  [[nodiscard]] u32 getWord(u32 offset) const;
  [[nodiscard]] u16 getShortBE(u32 offset) const;
  [[nodiscard]] u32 getWordBE(u32 offset) const;
  bool isValidOffset(u32 offset) const;
  // FIXME: clearChildren() is a workaround for VGMSeqNoTrks' multiple inheritance diamond problem

public:
  const Type type;

private:
  std::vector<std::unique_ptr<VGMItem>> m_ownedChildren;
  std::vector<VGMItem *> m_children;
  // Maintains sort + prefix-max end cache for fast offset lookups.
  bool m_childrenSorted = true;
  std::vector<u64> m_childrenPrefixMaxEnd;
  VGMFile *m_vgmfile;
  VGMItem *m_parent = nullptr;
  u32 m_offset;  // offset in the pDoc data buffer
  u32 m_length;  // num of bytes the event engulfs

  void ensureChildrenSorted();
  void rebuildChildPrefixMaxEnd();
  void invalidateParentCache(bool offsetChanged);
  std::string m_name;
};

struct ItemPtrOffsetCmp {
  bool operator()(const VGMItem *a, const VGMItem *b) const { return (a->offset() < b->offset()); }
};

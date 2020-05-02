#pragma once

#include "common.h"

template <class T>
class Menu;
class RawFile;
class VGMFile;
class VGMHeader;

class VGMItem {
public:
  enum ItemType { ITEMTYPE_UNDEFINED, ITEMTYPE_VGMFILE, ITEMTYPE_SEQEVENT };
  enum Icon {
    ICON_SEQ,
    ICON_INSTRSET,
    ICON_SAMPCOLL,
    ICON_UNKNOWN,
    ICON_NOTE,
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
    ICON_MAX
  };
  enum EventColor {
    CLR_UNKNOWN,
    CLR_UNRECOGNIZED,
    CLR_HEADER,
    CLR_MISC,
    CLR_MARKER,
    CLR_TIMESIG,
    CLR_TEMPO,
    CLR_PROGCHANGE,
    CLR_TRANSPOSE,
    CLR_PRIORITY,
    CLR_VOLUME,
    CLR_EXPRESSION,
    CLR_PAN,
    CLR_NOTEON,
    CLR_NOTEOFF,
    CLR_DURNOTE,
    CLR_TIE,
    CLR_REST,
    CLR_PITCHBEND,
    CLR_PITCHBENDRANGE,
    CLR_MODULATION,
    CLR_PORTAMENTO,
    CLR_PORTAMENTOTIME,
    CLR_CHANGESTATE,
    CLR_ADSR,
    CLR_LFO,
    CLR_REVERB,
    CLR_SUSTAIN,
    CLR_LOOP,
    CLR_LOOPFOREVER,
    CLR_TRACKEND,
  };

public:
  VGMItem();
  VGMItem(VGMFile *vgmfile,
          uint32_t offset,
          uint32_t length = 0,
          std::string name = "",
          EventColor color = CLR_UNKNOWN);

  friend bool operator>(VGMItem &item1, VGMItem &item2);
  friend bool operator<=(VGMItem &item1, VGMItem &item2);
  friend bool operator<(VGMItem &item1, VGMItem &item2);
  friend bool operator>=(VGMItem &item1, VGMItem &item2);

public:
  RawFile *GetRawFile();

  virtual bool IsItemAtOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false);
  virtual VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false);
  virtual uint32_t GuessLength() { return unLength; };
  virtual void SetGuessedLength(){};
  virtual std::vector<const char* > *GetMenuItemNames() { return nullptr; }
  virtual std::string GetDescription() { return ""; }
  [[nodiscard]] virtual ItemType GetType() const { return ITEMTYPE_UNDEFINED; }
  virtual Icon GetIcon() { return ICON_BINARY; }
  virtual void AddToUI(VGMItem *parent, void *UI_specific);
  virtual bool IsContainerItem() const { return false; }

protected:
  uint32_t GetBytes(uint32_t index, uint32_t count, void *buffer) const;
  [[nodiscard]] uint8_t GetByte(uint32_t offset) const;
  [[nodiscard]] uint16_t GetShort(uint32_t offset) const;
  [[nodiscard]] uint32_t GetWord(uint32_t offset) const;
  [[nodiscard]] uint16_t GetShortBE(uint32_t offset) const;
  [[nodiscard]] uint32_t GetWordBE(uint32_t offset) const;
  bool IsValidOffset(uint32_t offset) const;

public:
  EventColor color;
  VGMFile *vgmfile;
  std::string name;
  uint32_t dwOffset;  // offset in the pDoc data buffer
  uint32_t unLength;  // num of bytes the event engulfs
};

//  ****************
//  VGMContainerItem
//  ****************

class VGMContainerItem : public VGMItem {
public:
  VGMContainerItem();
  VGMContainerItem(VGMFile *vgmfile,
                   uint32_t offset,
                   uint32_t length = 0,
                   const std::string& name = "",
                   EventColor color = CLR_HEADER);
  virtual ~VGMContainerItem();

  VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false) override;
  uint32_t GuessLength() override;
  void SetGuessedLength() override;
  void AddToUI(VGMItem *parent, void *UI_specific) override;
  bool IsContainerItem() const override { return true; }

  VGMHeader *AddHeader(uint32_t offset, uint32_t length, const std::string &name = "Header");

  void AddItem(VGMItem *item);
  void AddSimpleItem(uint32_t offset, uint32_t length, const std::string &name);
  void AddUnknownItem(uint32_t offset, uint32_t length);

  template <class T>
  void AddContainer(std::vector<T *> &container) {
    containers.push_back((std::vector<VGMItem *> *)&container);
  }

  template <class T>
  bool RemoveContainer(std::vector<T *> &container) {
    auto iter = std::find(containers.begin(), containers.end(), (std::vector<VGMItem *> *)&container);
    if (iter != containers.end()) {
      containers.erase(iter);
      return true;
    } else {
      return false;
    }
  }

public:
  std::vector<VGMHeader *> headers;
  std::vector<std::vector<VGMItem *> *> containers;
  std::vector<VGMItem *> localitems;
};

struct ItemPtrOffsetCmp {
  bool operator()(const VGMItem *a, const VGMItem *b) const { return (a->dwOffset < b->dwOffset); }
};

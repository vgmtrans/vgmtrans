#pragma once

#include "common.h"

template <class T>
class Menu;
class RawFile;
class VGMFile;
class VGMItem;
class VGMHeader;

struct ItemSet {
  VGMItem *item;
  VGMItem *parent;
  const wchar_t *itemName;
};

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
  VGMItem(VGMFile *thevgmfile,
          uint32_t theOffset,
          uint32_t theLength = 0,
          const std::wstring theName = L"",
          EventColor color = CLR_UNKNOWN);
  virtual ~VGMItem();

  friend bool operator>(VGMItem &item1, VGMItem &item2);
  friend bool operator<=(VGMItem &item1, VGMItem &item2);
  friend bool operator<(VGMItem &item1, VGMItem &item2);
  friend bool operator>=(VGMItem &item1, VGMItem &item2);

public:
  virtual bool IsItemAtOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false);
  virtual VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false);
  virtual uint32_t GuessLength();
  virtual void SetGuessedLength();

  RawFile *GetRawFile();

  virtual std::vector<const wchar_t *> *GetMenuItemNames() { return nullptr; }
  virtual bool CallMenuItem(VGMItem *item, int menuItemNum) { return false; }
  virtual std::wstring GetDescription() { return name; }
  virtual ItemType GetType() const { return ITEMTYPE_UNDEFINED; }
  virtual Icon GetIcon() { return ICON_BINARY; /*ICON_UNKNOWN*/ }
  virtual void AddToUI(VGMItem *parent, void *UI_specific);
  virtual bool IsContainerItem() { return false; }

protected:
  // TODO make inline
  uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer);
  uint8_t GetByte(uint32_t offset);
  uint16_t GetShort(uint32_t offset);
  uint32_t GetWord(uint32_t offset);
  uint16_t GetShortBE(uint32_t offset);
  uint32_t GetWordBE(uint32_t offset);
  bool IsValidOffset(uint32_t offset);

public:
  EventColor color;
  VGMFile *vgmfile;
  std::wstring name;
  uint32_t dwOffset;  // offset in the pDoc data buffer
  uint32_t unLength;  // num of bytes the event engulfs
};

class VGMContainerItem : public VGMItem {
public:
  VGMContainerItem();
  VGMContainerItem(VGMFile *thevgmfile,
                   uint32_t theOffset,
                   uint32_t theLength = 0,
                   const std::wstring theName = L"",
                   EventColor color = CLR_HEADER);
  virtual ~VGMContainerItem();
  virtual VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false);
  virtual uint32_t GuessLength();
  virtual void SetGuessedLength();
  virtual void AddToUI(VGMItem *parent, void *UI_specific);
  virtual bool IsContainerItem() { return true; }

  VGMHeader *AddHeader(uint32_t offset, uint32_t length, const std::wstring &name = L"Header");

  void AddItem(VGMItem *item);
  void AddSimpleItem(uint32_t offset, uint32_t length, const std::wstring &theName);
  void AddUnknownItem(uint32_t offset, uint32_t length);

  template <class T>
  void AddContainer(std::vector<T *> &container) {
    containers.push_back((std::vector<VGMItem *> *)&container);
  }
  template <class T>
  bool RemoveContainer(std::vector<T *> &container) {
    std::vector<std::vector<VGMItem *> *>::iterator iter =
        std::find(containers.begin(), containers.end(), (std::vector<VGMItem *> *)&container);
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

class ItemPtrOffsetCmp {
public:
  bool operator()(const VGMItem *a, const VGMItem *b) const { return (a->dwOffset < b->dwOffset); }
};

template <class T>
VGMItem *GetItemAtOffsetInItemVector(uint32_t offset, std::vector<T *> &theArray) {
  int nArraySize = (int)theArray.size();
  for (int i = 0; i < nArraySize; i++) {
    if (((VGMItem *)theArray[i])->IsItemAtOffset(offset))
      return theArray[i];
  }
  return nullptr;
}

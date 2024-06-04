#pragma once

#include "common.h"
#include <algorithm>
#include <vector>
#include <ranges>
#include <typeindex>
#include <unordered_set>

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
  virtual ~VGMItem() = default;

  friend bool operator>(VGMItem &item1, VGMItem &item2);
  friend bool operator<=(VGMItem &item1, VGMItem &item2);
  friend bool operator<(VGMItem &item1, VGMItem &item2);
  friend bool operator>=(VGMItem &item1, VGMItem &item2);

  [[nodiscard]] std::string name() const noexcept { return m_name; }
  void setName(const std::string& newName) { m_name = newName; }

  [[nodiscard]] VGMFile* vgmFile() const { return m_vgmfile; }
  [[nodiscard]] RawFile* rawFile() const;

  virtual bool IsItemAtOffset(uint32_t offset, bool matchStartOffset = false);
  virtual VGMItem *GetItemFromOffset(uint32_t offset, bool matchStartOffset = false);
  // VGMItem *GetItemFromOffset(uint32_t offset, bool matchStartOffset = false);
  // virtual VGMItem* GetItemFromOffset(uint32_t offset, bool matchStartOffset = false, const std::unordered_set<std::type_index> & filterTypes = {});
  // virtual VGMItem* GetItemFromOffset(uint32_t offset, bool matchStartOffset,
                                   // const std::unordered_set<std::type_index>& filterTypes = {});
  virtual VGMItem* GetItemFromOffset(uint32_t offset, bool matchStartOffset, std::function<bool(const VGMItem*)> filterFunc);
  // VGMItem* GetItemFromOffset(uint32_t offset, bool matchStartOffset, std::function<bool(const VGMItem*)> filterFunc);
  // Template helper to create a filter function from a list of type arguments
  template<typename... FilterTypes>
  VGMItem* GetItemFromOffsetExcludingTypes(uint32_t offset, bool matchStartOffset) {
    auto filterFunc = [](const VGMItem* item) -> bool {
      return (... || dynamic_cast<const FilterTypes*>(item));
    };

    return GetItemFromOffset(offset, matchStartOffset, filterFunc);
  }

  // virtual VGMItem* GetItemFromOffset(uint32_t offset, bool matchStartOffset) {
    // return GetItemFromOffsetWithFilter<nullptr_t>(offset, matchStartOffset);
  // }

  // template<typename TFilter>
  // VGMItem* GetItemFromOffsetWithFilter(uint32_t offset, bool matchStartOffset) {
  //   for (const auto& child : m_children) {
  //     if (VGMItem* foundItem = child->GetItemFromOffsetWithFilter<TFilter>(offset, matchStartOffset)) {
  //       return foundItem;
  //     }
  //   }
  //
  //   // if constexpr (!std::is_same_v<TFilter, VGMItem>) {
  //   if constexpr (!std::is_base_of_v<VGMItem, TFilter>) {
  //     if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
  //       return this;
  //     }
  //   }
  //
  //   return nullptr;
  // }

  virtual uint32_t GuessLength();
  virtual void SetGuessedLength();
  virtual std::vector<const char* > *GetMenuItemNames() { return nullptr; }
  virtual std::string description() { return ""; }
  [[nodiscard]] virtual ItemType GetType() const { return ITEMTYPE_UNDEFINED; }
  virtual Icon GetIcon() { return ICON_BINARY; }
  virtual void AddToUI(VGMItem *parent, void *UI_specific);

  std::vector<VGMItem*> children() { return m_children; }
  VGMItem* addChild(VGMItem* child);
  VGMItem* addSimpleChild(uint32_t offset, uint32_t length, const std::string &name);
  VGMItem* addUnknownChild(uint32_t offset, uint32_t length);
  VGMHeader* addHeader(uint32_t offset, uint32_t length, const std::string &name = "Header");

  template <std::ranges::input_range Range>
  requires std::convertible_to<std::ranges::range_value_t<Range>, VGMItem*>
  void addChildren(const Range& items) {
    std::ranges::copy(items, std::back_inserter(m_children));
  }

protected:
  uint32_t GetBytes(uint32_t index, uint32_t count, void *buffer) const;
  [[nodiscard]] uint8_t GetByte(uint32_t offset) const;
  [[nodiscard]] uint16_t GetShort(uint32_t offset) const;
  [[nodiscard]] uint32_t GetWord(uint32_t offset) const;
  [[nodiscard]] uint16_t GetShortBE(uint32_t offset) const;
  [[nodiscard]] uint32_t GetWordBE(uint32_t offset) const;
  bool IsValidOffset(uint32_t offset) const;

public:
  uint32_t dwOffset;  // offset in the pDoc data buffer
  uint32_t unLength;  // num of bytes the event engulfs
  EventColor color;

private:
  std::vector<VGMItem *> m_children;
  VGMFile *m_vgmfile;
  std::string m_name;
};

//  ****************
//  VGMContainerItem
//  ****************
//
// class VGMContainerItem : public VGMItem {
// public:
//   VGMContainerItem();
//   VGMContainerItem(VGMFile *vgmfile,
//                    uint32_t offset,
//                    uint32_t length = 0,
//                    std::string name = "",
//                    EventColor color = CLR_HEADER);
//   virtual ~VGMContainerItem();
//
//   VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true, bool matchStartOffset = false) override;
//   uint32_t GuessLength() override;
//   void SetGuessedLength() override;
//   void AddToUI(VGMItem *parent, void *UI_specific) override;
//   bool IsContainerItem() const override { return true; }
//
//   VGMHeader *addHeader(uint32_t offset, uint32_t length, const std::string &name = "Header");
//
//   void AddItem(VGMItem *item);
//   void AddSimpleItem(uint32_t offset, uint32_t length, const std::string &name);
//   void addUnknownChild(uint32_t offset, uint32_t length);
//
//   template <class T>
//   void AddContainer(std::vector<T*>& container) {
//     static_assert(std::is_base_of_v<VGMItem, T>, "T must be a subclass of VGMItem");
//     containers.push_back(reinterpret_cast<std::vector<VGMItem *> *>(&container));
//   }
//
//   template <class T>
//   bool RemoveContainer(std::vector<T *> &container) {
//     auto iter = std::ranges::find(containers, reinterpret_cast<std::vector<VGMItem*>*>(&container));
//     if (iter != containers.end()) {
//       containers.erase(iter);
//       return true;
//     } else {
//       return false;
//     }
//   }
//
// public:
//   std::vector<VGMHeader *> headers;
//   std::vector<std::vector<VGMItem *> *> containers;
//   std::vector<VGMItem *> localitems;
// };

struct ItemPtrOffsetCmp {
  bool operator()(const VGMItem *a, const VGMItem *b) const { return (a->dwOffset < b->dwOffset); }
};

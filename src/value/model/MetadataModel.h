/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/base/CoreTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

// Item nodes describe what the UI can click in the source bytes. They point back
// to parsed data, but they are not the parsed data themselves.
enum class ItemKind {
  Source,
  Header,
  Sequence,
  Track,
  Command,
  InstrumentSet,
  Instrument,
  Region,
  SampleCollection,
  Sample,
  Misc,
};

struct ItemNode {
  ItemId id;
  std::optional<ItemId> parent;
  ItemKind kind = ItemKind::Misc;
  std::string detailKind;
  std::string name;
  std::string description;
  SourceRange range;
  std::vector<ItemId> children;
};

struct ItemTree {
  // Source-backed outline for HexView/tree navigation.
  std::optional<ItemId> root;
  std::vector<ItemNode> nodes;
};

// Common metadata for sequences, instrument sets, sample collections, and misc
// assets. Exporters use this for names and source ranges instead of rediscovering them.
struct AssetMetadata {
  AssetId id;
  std::string format;
  std::string name;
  SourceRange range;
  ItemTree items;
};

// Address is the driver's address value. It may differ from a file offset after
// a source has been extracted or remapped.
struct Address {
  u64 value = 0;
};

struct Timebase {
  u32 ppqn = 48;
};

// How export should treat source loops. The parsed sequence still keeps the
// original loop commands for formats that can represent them directly.
enum class LoopPolicy {
  Default,
  PlayOnce,
  Preserve,
};

}  // namespace vgmtrans::core

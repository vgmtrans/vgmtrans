/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/base/CoreTypes.h"

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

// One immutable, format-owned value retained with a scanned asset. The core
// deliberately cannot inspect or serialize it; typed format code uses it to
// carry source meaning needed after collection matching without reparsing.
class AssetPrivateData {
public:
  AssetPrivateData() = default;

  template <typename T>
  [[nodiscard]] static AssetPrivateData make(T value) {
    using Value = std::remove_cvref_t<T>;
    static_assert(std::is_object_v<Value>);
    return AssetPrivateData(std::make_shared<const Value>(std::move(value)));
  }

  template <typename T>
  [[nodiscard]] const std::remove_cvref_t<T>* get() const noexcept {
    using Value = std::remove_cvref_t<T>;
    const auto* owned = std::any_cast<std::shared_ptr<const Value>>(&value_);
    return owned != nullptr ? owned->get() : nullptr;
  }

  [[nodiscard]] bool empty() const noexcept { return !value_.has_value(); }

private:
  explicit AssetPrivateData(std::any value) : value_(std::move(value)) {}

  std::any value_;
};

// Common metadata for sequences, sound banks, sample pools, and misc
// assets. range identifies the asset's primary source structure; SourceInspection
// expands it across the asset-owned annotation graph when presenting its bytes.
struct AssetMetadata {
  AssetId id;
  std::string format;
  std::string name;
  SourceRange range;
};

// Address is the canonical sequence bytecode/source offset used by TrackProgram
// and SequenceVm. Formats must translate raw driver pointers into this address
// space before handing them to VM flow helpers such as jump, call, or target.
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

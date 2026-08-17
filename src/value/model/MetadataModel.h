/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/base/CoreTypes.h"
#include "value/base/TypeToken.h"

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
    auto owned = std::make_shared<const Value>(std::move(value));
    return AssetPrivateData(detail::typeToken<Value>(), std::move(owned));
  }

  template <typename T>
  [[nodiscard]] const std::remove_cvref_t<T>* get() const noexcept {
    using Value = std::remove_cvref_t<T>;
    return type_ == detail::typeToken<Value>() ? static_cast<const Value*>(value_.get()) : nullptr;
  }

  [[nodiscard]] bool empty() const noexcept { return value_ == nullptr; }

private:
  AssetPrivateData(const void* type, std::shared_ptr<const void> value) : type_(type), value_(std::move(value)) {}

  const void* type_ = nullptr;
  std::shared_ptr<const void> value_;
};

// Common metadata for sequences, instrument sets, sample collections, and misc
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

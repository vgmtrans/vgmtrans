/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace vgmtrans::core::detail {

// Execution-only state for formats whose source represents notes as matched
// Note On and Note Off commands. PerformanceEmitter is the format-facing API;
// the VM owns one tracker for the lifetime of each rendered track.
class ActiveNoteTracker {
public:
  struct Key {
    PerformanceLaneId lane;
    s32 value = 0;

    friend bool operator<(const Key& lhs, const Key& rhs) noexcept {
      return std::tie(lhs.lane.value, lhs.value) < std::tie(rhs.lane.value, rhs.value);
    }
  };

  struct Note {
    PerformanceNoteId id;
    size_t eventIndex = 0;
    std::optional<size_t> sourceSpanIndex;
    bool released = false;
  };

  [[nodiscard]] Note* find(Key key) {
    const auto found = notes_.find(key);
    return found != notes_.end() ? &found->second : nullptr;
  }

  [[nodiscard]] std::optional<Note> take(Key key) {
    auto found = notes_.find(key);
    if (found == notes_.end()) {
      return std::nullopt;
    }
    Note note = std::move(found->second);
    notes_.erase(found);
    return note;
  }

  void insert(Key key, Note note) { notes_.insert_or_assign(key, std::move(note)); }

  [[nodiscard]] std::vector<Note> takeReleased() {
    std::vector<Note> released;
    for (auto note = notes_.begin(); note != notes_.end();) {
      if (!note->second.released) {
        ++note;
        continue;
      }
      released.push_back(std::move(note->second));
      note = notes_.erase(note);
    }
    return released;
  }

  [[nodiscard]] std::vector<Note> takeAll() {
    std::vector<Note> active;
    active.reserve(notes_.size());
    for (auto& [_, note] : notes_) {
      active.push_back(std::move(note));
    }
    notes_.clear();
    return active;
  }

  [[nodiscard]] bool sustainPedal() const noexcept { return sustainPedal_; }
  void setSustainPedal(bool down) noexcept { sustainPedal_ = down; }

  void bindSourceSpan(size_t firstEvent, size_t pastLastEvent, std::vector<SourcePlaybackSpan>& sourceSpans,
                      size_t sourceSpanIndex) {
    sourceSpans_ = &sourceSpans;
    for (auto& [_, note] : notes_) {
      if (note.eventIndex >= firstEvent && note.eventIndex < pastLastEvent) {
        note.sourceSpanIndex = sourceSpanIndex;
      }
    }
  }

  void extendSourceSpan(std::optional<size_t> sourceSpanIndex, u64 endTick) const {
    if (sourceSpans_ == nullptr || !sourceSpanIndex || *sourceSpanIndex >= sourceSpans_->size()) {
      return;
    }
    auto& span = (*sourceSpans_)[*sourceSpanIndex];
    span.endTick = std::max(span.endTick, endTick);
  }

private:
  std::map<Key, Note> notes_;
  std::vector<SourcePlaybackSpan>* sourceSpans_ = nullptr;
  bool sustainPedal_ = false;
};

}  // namespace vgmtrans::core::detail

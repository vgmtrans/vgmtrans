/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vgmtrans::core {

template <typename T>
class SharedSequence;

namespace detail {

class SharedSequenceAccess {
public:
  template <typename T>
  [[nodiscard]] static SharedSequence<T> fromChunks(std::vector<std::shared_ptr<const std::vector<T>>> chunks) {
    return SharedSequence<T>{std::move(chunks)};
  }
};

}  // namespace detail

// An immutable logical sequence whose backing values may be shared in several
// contiguous chunks. Chunk boundaries are deliberately absent from the public
// interface: callers get ordinary ordered iteration and random access.
template <typename T>
class SharedSequence {
  struct Storage;

public:
  class const_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    const_iterator() = default;

    [[nodiscard]] reference operator*() const { return (*storage_->chunks[chunk_])[offset_]; }

    [[nodiscard]] pointer operator->() const { return std::addressof(**this); }

    const_iterator& operator++() {
      ++offset_;
      if (offset_ == storage_->chunks[chunk_]->size()) {
        ++chunk_;
        offset_ = 0;
      }
      return *this;
    }

    const_iterator operator++(int) {
      auto previous = *this;
      ++*this;
      return previous;
    }

    friend bool operator==(const const_iterator&, const const_iterator&) = default;

  private:
    friend class SharedSequence;

    const_iterator(const Storage* storage, size_t chunk, size_t offset)
        : storage_(storage), chunk_(chunk), offset_(offset) {}

    const Storage* storage_ = nullptr;
    size_t chunk_ = 0;
    size_t offset_ = 0;
  };

  SharedSequence() : storage_(emptyStorage()) {}

  explicit SharedSequence(std::vector<T> values)
      : SharedSequence(std::vector<std::shared_ptr<const std::vector<T>>>{
            std::make_shared<const std::vector<T>>(std::move(values)),
        }) {}

  [[nodiscard]] bool empty() const noexcept { return storage_->size == 0; }
  [[nodiscard]] size_t size() const noexcept { return storage_->size; }

  [[nodiscard]] const T& operator[](size_t index) const noexcept {
    const auto found = std::upper_bound(storage_->ends.begin(), storage_->ends.end(), index);
    const size_t chunk = static_cast<size_t>(found - storage_->ends.begin());
    const size_t begin = chunk == 0 ? 0 : storage_->ends[chunk - 1];
    return (*storage_->chunks[chunk])[index - begin];
  }

  [[nodiscard]] const T& at(size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("SharedSequence index outside bounds");
    }
    return (*this)[index];
  }

  [[nodiscard]] const T& front() const noexcept { return (*this)[0]; }
  [[nodiscard]] const T& back() const noexcept { return (*this)[size() - 1]; }

  [[nodiscard]] const_iterator begin() const noexcept { return {storage_.get(), 0, 0}; }
  [[nodiscard]] const_iterator end() const noexcept { return {storage_.get(), storage_->chunks.size(), 0}; }

private:
  friend class detail::SharedSequenceAccess;

  struct Storage {
    explicit Storage(std::vector<std::shared_ptr<const std::vector<T>>> values) {
      chunks.reserve(values.size());
      ends.reserve(values.size());
      for (auto& chunk : values) {
        if (chunk == nullptr || chunk->empty()) {
          continue;
        }
        size += chunk->size();
        chunks.push_back(std::move(chunk));
        ends.push_back(size);
      }
    }

    std::vector<std::shared_ptr<const std::vector<T>>> chunks;
    std::vector<size_t> ends;
    size_t size = 0;
  };

  explicit SharedSequence(std::vector<std::shared_ptr<const std::vector<T>>> chunks)
      : storage_(std::make_shared<const Storage>(std::move(chunks))) {}

  [[nodiscard]] static std::shared_ptr<const Storage> emptyStorage() {
    static const auto empty = std::make_shared<const Storage>(std::vector<std::shared_ptr<const std::vector<T>>>{});
    return empty;
  }

  std::shared_ptr<const Storage> storage_;
};

}  // namespace vgmtrans::core

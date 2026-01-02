// Byte pattern class for flexible byte sequence search
// Heavily inspired by SigScan at GameDeception.net

#pragma once
#include "common.h"

class BytePattern {
 private:
  /** The pattern to scan for */
  char *ptn_str;

  /**
   * A mask to ignore certain bytes in the pattern such as addresses.
   * The mask should be as long as all the bytes in the pattern.
   * Use '?' to ignore a byte and 'x' to check it.
   * Example: "xxx????xx" - The first 3 bytes are checked, then the next 4 are ignored,
   * then the last 2 are checked
   */
  char *ptn_mask;

  /** The length of ptn_str and ptn_mask (not including a terminating null for ptn_mask) */
  size_t ptn_len;

 public:
  BytePattern();
  BytePattern(const char *pattern, size_t length);
  BytePattern(const char *pattern, const char *mask, size_t length);
  BytePattern(const BytePattern &obj);
  ~BytePattern();

  bool match(const void *buf, size_t buf_len) const;
  bool search(const void *buf, size_t buf_len, size_t &match_offset, size_t search_offset = 0) const;
  inline size_t length() const { return ptn_len; }
};

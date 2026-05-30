/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

// Byte pattern class for flexible byte sequence search
// Heavily inspired by SigScan at GameDeception.net

#include "BytePattern.h"

#include <assert.h>
#include <cstring>
#include <memory>

BytePattern::BytePattern() :
    ptn_len(0) {
}

BytePattern::BytePattern(const char *pattern, size_t length) :
    ptn_len(length) {
  if (length != 0) {
    if (pattern != NULL) {
      ptn_str = std::make_unique<char[]>(length);
      memcpy(ptn_str.get(), pattern, length);
    }
  }
}

BytePattern::BytePattern(const char *pattern, const char *mask, size_t length) :
    ptn_len(length) {
  if (length != 0) {
    if (pattern != NULL) {
      ptn_str = std::make_unique<char[]>(length);
      memcpy(ptn_str.get(), pattern, length);

      if (mask != NULL) {
        assert(strlen(mask) == length);

        ptn_mask = std::make_unique<char[]>(length);
        memcpy(ptn_mask.get(), mask, length);
      }
    }
  }
}

BytePattern::BytePattern(const BytePattern &obj) :
    ptn_len(obj.ptn_len) {
  if (obj.ptn_str != NULL) {
    ptn_str = std::make_unique<char[]>(ptn_len);
    memcpy(ptn_str.get(), obj.ptn_str.get(), ptn_len);
  }

  if (obj.ptn_mask != NULL) {
    ptn_mask = std::make_unique<char[]>(ptn_len);
    memcpy(ptn_mask.get(), obj.ptn_mask.get(), ptn_len);
  }
}

BytePattern& BytePattern::operator=(const BytePattern& obj) {
  if (this == &obj) {
    return *this;
  }

  ptn_len = obj.ptn_len;
  ptn_str.reset();
  ptn_mask.reset();

  if (obj.ptn_str != NULL) {
    ptn_str = std::make_unique<char[]>(ptn_len);
    memcpy(ptn_str.get(), obj.ptn_str.get(), ptn_len);
  }

  if (obj.ptn_mask != NULL) {
    ptn_mask = std::make_unique<char[]>(ptn_len);
    memcpy(ptn_mask.get(), obj.ptn_mask.get(), ptn_len);
  }

  return *this;
}

bool BytePattern::match(const void *buf, size_t buf_len) const {
  if (buf == NULL)
    return false;

  if (ptn_str == NULL)
    return false;

  if (ptn_len > buf_len)
    return false;

  for (size_t i = 0; i < ptn_len; i++) {
    // '?' matches to any characters
    if (ptn_mask == NULL || ptn_mask[i] != '?') {
      if (((const char *) buf)[i] != ptn_str[i]) {
        return false;
      }
    }
  }
  return true;
}

bool BytePattern::search(const void *buf, size_t buf_len, size_t &match_offset, size_t search_offset) const {
  if (buf == NULL)
    return false;

  if (ptn_str == NULL)
    return false;

  if (search_offset + ptn_len > buf_len)
    return false;

  for (size_t i = search_offset; i <= buf_len - ptn_len; i++) {
    if (match((const char *) buf + i, ptn_len)) {
      match_offset = i;
      return true;
    }
  }
  return false;
}

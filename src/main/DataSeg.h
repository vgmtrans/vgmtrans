#pragma once

#include "pch.h"
#include <deque>

//DataSeg is a very simple class for allocating a block of data with a
//variable reference point for indexing.  Some formats use
//absolute offset references (the Nintendo SNES seq format, for example)

class DataSeg {
 public:
  DataSeg();
  ~DataSeg();

  inline uint8_t &operator[](uint32_t offset) const {
    assert(offset >= startOff && (offset < (startOff + size)));
    return data[offset - startOff];
  }

 public:
  void reposition(uint32_t newBegin);
  void load(uint8_t *buf, uint32_t startVirtAddr, uint32_t theSize);
  void alloc(uint32_t theSize);
  void clear();


  inline void GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const {
    assert((nIndex >= startOff) && (nIndex + nCount <= endOff));
    memcpy(pBuffer, data + nIndex - startOff, nCount);
  }

  [[nodiscard]] inline uint8_t GetByte(uint32_t nIndex) const {
    assert((nIndex >= startOff) && (nIndex + 1 <= endOff));
    return data[nIndex - startOff];
  }

  [[nodiscard]] inline uint16_t GetShort(uint32_t nIndex) const {
    assert((nIndex >= startOff) && (nIndex + 2 <= endOff));
    return static_cast<uint16_t>(data[nIndex - startOff])
           | static_cast<uint16_t>(data[nIndex + 1 - startOff]) << 8;
  }

  [[nodiscard]] inline uint32_t GetWord(uint32_t nIndex) const {
    assert((nIndex >= startOff) && (nIndex + 4 <= endOff));
    return static_cast<uint32_t>(data[nIndex - startOff])
           | static_cast<uint32_t>(data[nIndex + 1 - startOff]) << 8
           | static_cast<uint32_t>(data[nIndex + 2 - startOff]) << 16
           | static_cast<uint32_t>(data[nIndex + 3 - startOff]) << 24;
  }

  [[nodiscard]] inline uint16_t GetShortBE(uint32_t nIndex) const {
    assert((nIndex >= startOff) && (nIndex + 2 <= endOff));
    return (static_cast<uint16_t>(data[nIndex - startOff]) << 8)
           + static_cast<uint16_t>(data[nIndex + 1 - startOff]);
  }

  [[nodiscard]] inline uint32_t GetWordBE(uint32_t nIndex) const {
    assert((nIndex >= startOff) && (nIndex + 4 <= endOff));
    return (static_cast<uint32_t>(data[nIndex - startOff]) << 24)
           + (static_cast<uint32_t>(data[nIndex + 1 - startOff]) << 16)
           + (static_cast<uint32_t>(data[nIndex + 2 - startOff]) << 8)
           + static_cast<uint32_t>(data[nIndex + 3 - startOff]);
  }

  [[nodiscard]] inline bool IsValidOffset(uint32_t nIndex) const {
    return (nIndex >= startOff && nIndex < endOff);
  }


 public:
  uint8_t *data;

  uint32_t startOff;
  uint32_t endOff;
  uint32_t size;
  bool bAlloced;
};

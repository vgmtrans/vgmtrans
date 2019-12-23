/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

// DataSeg is a very simple class for allocating a block of data with a
// variable reference point for indexing.  Some formats use
// absolute offset references (the Nintendo SNES seq format, for example)

#include <cstdint>
#include <cassert>
#include <cstring>

class DataSeg {
   public:
    DataSeg(void);
    ~DataSeg(void);

    void reposition(uint32_t newBegin);
    void load(uint8_t *buf, uint32_t startVirtAddr, uint32_t theSize);
    void alloc(uint32_t theSize);
    void clear();

    uint8_t &operator[](size_t offset);

    inline void GetBytes(size_t offset, uint32_t nCount, void *pBuffer) {
        assert((offset >= startOff) && (offset + nCount <= endOff));
        memcpy(pBuffer, data + offset - startOff, nCount);
    }

    inline uint8_t GetByte(size_t offset) const {
        assert((offset >= startOff) && (offset + 1 <= endOff));
        return data[offset - startOff];
    }

    inline uint16_t GetShort(size_t offset) const {
        assert((offset >= startOff) && (offset + 2 <= endOff));
        return *((uint16_t *)(data + offset - startOff));
    }

    inline uint32_t GetWord(size_t offset) const {
        assert((offset >= startOff) && (offset + 4 <= endOff));
        return *((uint32_t *)(data + offset - startOff));
    }

    inline uint16_t GetShortBE(size_t offset) const {
        assert((offset >= startOff) && (offset + 2 <= endOff));
        return ((uint8_t)(data[offset - startOff]) << 8) + ((uint8_t)data[offset + 1 - startOff]);
    }

    inline uint32_t GetWordBE(size_t offset) const {
        assert((offset >= startOff) && (offset + 4 <= endOff));
        return ((uint8_t)data[offset - startOff] << 24) +
               ((uint8_t)data[offset + 1 - startOff] << 16) +
               ((uint8_t)data[offset + 2 - startOff] << 8) + (uint8_t)data[offset + 3 - startOff];
    }

    inline bool IsValidOffset(size_t offset) const {
        return (offset >= startOff && offset < endOff);
    }

   public:
    uint8_t *data;

    uint32_t startOff;
    uint32_t endOff;
    uint32_t size;
    bool bAlloced;
};

#pragma once

#include <cstdint>
#include <cassert>
#include <cstring>
#include <memory>
#include <iterator>

/*
 * A relatively simple class for allocating a block of m_data
 * with a variable reference point for indexing.
 * Mostly acts as a file-buffer.
 * Handy when dealing with formats that use absolute offset references
 * (e.g. SNES sequences)
 */
class DataBlock {
   public:
    DataBlock(std::uint32_t start_addr, std::size_t size);
    DataBlock(std::uint8_t *data, std::uint32_t start_addr, std::size_t size);

    void relocate(uint32_t new_address);

    uint32_t beginOffset() const noexcept { return m_start_off; }
    void setBeginOffset(uint32_t offset) noexcept { m_start_off = offset; }
    uint32_t endOffset() const noexcept { return m_end_off; }
    void setEndOffset(uint32_t offset) noexcept { m_end_off = offset; }

    /* Legacy interface */
    inline void GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) {
        assert((nIndex >= m_start_off) && (nIndex + nCount <= m_end_off));
        memcpy(pBuffer, m_data.get() + nIndex - m_start_off, nCount);
    }

    inline uint8_t GetByte(uint32_t nIndex) {
        assert((nIndex >= m_start_off) && (nIndex + 1 <= m_end_off));
        return m_data[nIndex - m_start_off];
    }

    inline uint16_t GetShort(uint32_t nIndex) {
        assert((nIndex >= m_start_off) && (nIndex + 2 <= m_end_off));
        return *((uint16_t *)(m_data.get() + nIndex - m_start_off));
    }

    inline uint32_t GetWord(uint32_t nIndex) {
        assert((nIndex >= m_start_off) && (nIndex + 4 <= m_end_off));
        return *((uint32_t *)(m_data.get() + nIndex - m_start_off));
    }

    inline uint16_t GetShortBE(uint32_t nIndex) {
        assert((nIndex >= m_start_off) && (nIndex + 2 <= m_end_off));
        return ((uint8_t)(m_data[nIndex - m_start_off]) << 8) +
               ((uint8_t)m_data[nIndex + 1 - m_start_off]);
    }

    inline uint32_t GetWordBE(uint32_t nIndex) {
        assert((nIndex >= m_start_off) && (nIndex + 4 <= m_end_off));
        return ((uint8_t)m_data[nIndex - m_start_off] << 24) +
               ((uint8_t)m_data[nIndex + 1 - m_start_off] << 16) +
               ((uint8_t)m_data[nIndex + 2 - m_start_off] << 8) +
               (uint8_t)m_data[nIndex + 3 - m_start_off];
    }

    /* Iterators */
    uint8_t *begin() noexcept;
    const uint8_t *begin() const noexcept;

    uint8_t *end() noexcept;
    const uint8_t *end() const noexcept;

    const uint8_t *cbegin() const noexcept;
    const uint8_t *cend() const noexcept;

    const std::reverse_iterator<uint8_t *> rbegin() noexcept;
    const std::reverse_iterator<const uint8_t *> rbegin() const noexcept;

    const std::reverse_iterator<uint8_t *> rend() noexcept;
    const std::reverse_iterator<const uint8_t *> rend() const noexcept;

    const std::reverse_iterator<const uint8_t *> crbegin() const noexcept;
    const std::reverse_iterator<const uint8_t *> crend() const noexcept;

    /* Capacity */
    size_t size() const noexcept;
    size_t max_size() const noexcept;

    /* Access */
    uint8_t &operator[](uint32_t offset);
    const uint8_t &operator[](uint32_t offset) const;

    uint8_t *data() noexcept;
    const uint8_t *data() const noexcept;

   private:
    std::unique_ptr<uint8_t[]> m_data;
    std::uint32_t m_start_off;
    std::uint32_t m_end_off; /* Internal usage */
    std::size_t m_size;
};

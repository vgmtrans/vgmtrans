#include "DataBlock.h"

#include <cstring>
#include <cassert>

DataBlock::DataBlock(std::uint8_t *data, std::uint32_t start_addr, std::size_t size)
    : m_start_off(start_addr), m_end_off(start_addr + size), m_size(size) {
    m_data.reset(data);
}

DataBlock::DataBlock(std::uint32_t start_addr, std::size_t size)
    : m_data(std::make_unique<uint8_t[]>(size)),
      m_start_off(start_addr),
      m_end_off(start_addr),
      m_size(size) {}

void DataBlock::relocate(uint32_t newoffset) {
    if (newoffset < m_end_off && newoffset >= m_start_off) {
        uint32_t diff = newoffset - m_start_off;
        /* Don't do anything if there's no need to move */
        if (diff == 0) {
            return;
        }

        memmove(m_data.get(), m_data.get() + diff, m_size - diff);
    } else if (newoffset + m_size <= m_end_off && newoffset + m_size >= m_start_off) {
        uint32_t diff = m_end_off - (newoffset + m_size);
        memmove(m_data.get() + diff, m_data.get(), m_size - diff);
    }

    m_start_off = newoffset;
    m_end_off = m_start_off + m_size;
}

/* Iterators */

uint8_t *DataBlock::begin() noexcept {
    return m_data.get();
}

const uint8_t *DataBlock::begin() const noexcept {
    return m_data.get();
}

uint8_t *DataBlock::end() noexcept {
    return m_data.get() + (m_end_off - m_start_off);
}

const uint8_t *DataBlock::end() const noexcept {
    return m_data.get() + (m_end_off - m_start_off);
}

const uint8_t *DataBlock::cbegin() const noexcept {
    return m_data.get();
}

const uint8_t *DataBlock::cend() const noexcept {
    return m_data.get() + (m_end_off - m_start_off);
}

const std::reverse_iterator<uint8_t *> DataBlock::rbegin() noexcept {
    return std::reverse_iterator<uint8_t *>(end());
}

const std::reverse_iterator<const uint8_t *> DataBlock::rbegin() const noexcept {
    return std::reverse_iterator<const uint8_t *>(end());
}

const std::reverse_iterator<uint8_t *> DataBlock::rend() noexcept {
    return std::reverse_iterator<uint8_t *>(begin());
}
const std::reverse_iterator<const uint8_t *> DataBlock::rend() const noexcept {
    return std::reverse_iterator<const uint8_t *>(begin());
}

const std::reverse_iterator<const uint8_t *> DataBlock::crbegin() const noexcept {
    return std::reverse_iterator<const uint8_t *>(cend());
}
const std::reverse_iterator<const uint8_t *> DataBlock::crend() const noexcept {
    return std::reverse_iterator<const uint8_t *>(cbegin());
}

/* Capacity */

size_t DataBlock::size() const noexcept {
    return m_size;
}
size_t DataBlock::max_size() const noexcept {
    return m_size;
}

/* Access */
uint8_t &DataBlock::operator[](uint32_t offset) {
    assert(offset >= m_start_off && (offset < (m_start_off + m_size)));
    return m_data[offset - m_start_off];
}

const uint8_t &DataBlock::operator[](uint32_t offset) const {
    assert(offset >= m_start_off && (offset < (m_start_off + m_size)));
    return m_data[offset - m_start_off];
}

uint8_t *DataBlock::data() noexcept {
    return m_data.get();
}

const uint8_t *DataBlock::data() const noexcept {
    return m_data.get();
}

/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RiffFile.h"

uint32_t Chunk::size() {
  return 8 + paddedSize(m_size);
}

void Chunk::setData(const void *src, uint32_t datasize) {
  m_size = datasize;

  // set the size and copy from the data source
  datasize = paddedSize(m_size);
  if (data != nullptr) {
    delete[] data;
    data = nullptr;
  }
  data = new uint8_t[datasize];
  memcpy(data, src, m_size);

  // Add pad byte
  uint32_t padsize = datasize - m_size;
  if (padsize != 0) {
    memset(data + m_size, 0, padsize);
  }
}

void Chunk::write(uint8_t *buffer) {
  uint32_t padsize = paddedSize(m_size) - m_size;
  memcpy(buffer, id, 4);
  *reinterpret_cast<uint32_t*>(buffer + 4) = m_size + padsize; // Microsoft says the chunkSize doesn't contain padding size, but many software cannot handle the alignment.
  memcpy(buffer + 8, data, paddedSize(m_size));
}

Chunk *ListTypeChunk::addChildChunk(Chunk *ck) {
  childChunks.push_back(ck);
  return ck;
}

uint32_t ListTypeChunk::size() {
  uint32_t size = 12;        //id + size + "LIST"
  for (auto iter = this->childChunks.begin(); iter != childChunks.end(); ++iter)
    size += (*iter)->size();
  return paddedSize(size);
}

void ListTypeChunk::write(uint8_t *buffer) {
  memcpy(buffer, this->id, 4);
  memcpy(buffer + 8, this->type, 4);

  uint32_t bufOffset = 12;
  for (auto iter = this->childChunks.begin(); iter != childChunks.end(); ++iter) {
    (*iter)->write(buffer + bufOffset);
    bufOffset += (*iter)->size();
  }

  uint32_t size = bufOffset;
  uint32_t padsize = paddedSize(size) - size;
  *reinterpret_cast<uint32_t*>(buffer + 4) = size + padsize - 8; // Microsoft says the chunkSize doesn't contain padding size, but many software cannot handle the alignment.

  // Add pad byte
  if (padsize != 0) {
    memset(data + size, 0, padsize);
  }
}

RiffFile::RiffFile(const std::string& file_name, const std::string& form)
    : RIFFChunk(form),
      name(file_name) {
}

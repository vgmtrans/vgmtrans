/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "util/types.h"
#include "RiffFile.h"

u32 Chunk::size() {
  return 8 + paddedSize(m_size);
}

void Chunk::setData(const void *src, u32 datasize) {
  m_size = datasize;

  // set the size and copy from the data source
  datasize = paddedSize(m_size);
  if (data != nullptr) {
    delete[] data;
    data = nullptr;
  }
  data = new u8[datasize];
  memcpy(data, src, m_size);

  // Add pad byte
  u32 padsize = datasize - m_size;
  if (padsize != 0) {
    memset(data + m_size, 0, padsize);
  }
}

void Chunk::write(u8 *buffer) {
  u32 padsize = paddedSize(m_size) - m_size;

  memcpy(buffer, id, 4);

  u32 value = m_size + padsize;
  memcpy(buffer + 4, &value, sizeof(value));

  memcpy(buffer + 8, data, paddedSize(m_size));
}

Chunk *ListTypeChunk::addChildChunk(Chunk *ck) {
  childChunks.push_back(ck);
  return ck;
}

u32 ListTypeChunk::size() {
  u32 size = 12;        //id + size + "LIST"
  for (auto iter = this->childChunks.begin(); iter != childChunks.end(); ++iter)
    size += (*iter)->size();
  return paddedSize(size);
}

void ListTypeChunk::write(u8 *buffer) {
  memcpy(buffer, this->id, 4);
  memcpy(buffer + 8, this->type, 4);

  size_t bufOffset = 12;
  for (auto iter = this->childChunks.begin(); iter != this->childChunks.end(); ++iter) {
    (*iter)->write(buffer + bufOffset);
    bufOffset += (*iter)->size();
  }

  u32 size = static_cast<u32>(bufOffset);
  u32 padsize = paddedSize(size) - size;

  u32 value = size + padsize - 8;
  memcpy(buffer + 4, &value, sizeof(value));
  // Add pad byte
  if (padsize != 0) {
    memset(buffer + size, 0, padsize);
  }
}

RiffFile::RiffFile(const std::string& file_name, const std::string& form)
    : RIFFChunk(form),
      name(file_name) {
}


#pragma once

#include <cstring>
#include "common.h"
#include "helper.h"


//////////////////////////////////////////////
// Chunk		- Riff format chunk
//////////////////////////////////////////////
class Chunk {
 public:
  char id[4];        //  A chunk ID identifies the type of data within the chunk.
  u8 *data;        //  The actual data not including a possible pad byte to word align

 public:
  Chunk(const std::string& theId)
      : m_size(0), data(nullptr) {
    assert(theId.length() == 4);
    memcpy(id, theId.c_str(), 4);
  }
  virtual ~Chunk() {
    if (data != nullptr) {
      delete[] data;
      data = nullptr;
    }
  }
  void setData(const void *src, u32 datasize);
  virtual u32 size();    //  Returns the size of the chunk in bytes, including any pad byte.
  void setSize(u32 size) { m_size = size; }

  virtual void write(u8 *buffer);

 protected:
  static inline u32 paddedSize(u32 size) {
    return size + (size % 2);
  }

private:
  u32 m_size;        //  The size of the chunk data in bytes, excluding any pad byte.
};


////////////////////////////////////////////////////////////////////////////
// ListTypeChunk	- Riff chunk type where the first 4 data bytes are a sig
//					  and the rest of the data is a collection of child chunks
////////////////////////////////////////////////////////////////////////////
class ListTypeChunk: public Chunk {
 public:
  char type[4];    // 4 byte sig that begins the data field, "LIST" or "sfbk" for ex
  std::list<Chunk *> childChunks;

 public:
  ListTypeChunk(const std::string& theId, const std::string& theType)
      : Chunk(theId) {
    assert(theType.length() == 4);
    memcpy(type, theType.c_str(), 4);
  }
  ~ListTypeChunk() override {
    deleteList(childChunks);
  }

  Chunk *addChildChunk(Chunk *ck);
  u32 size() override;    //  Returns the size of the chunk in bytes, including any pad byte.
  void write(u8 *buffer) override;
};

////////////////////////////////////////////////////////////////////////////
// RIFFChunk
////////////////////////////////////////////////////////////////////////////
class RIFFChunk: public ListTypeChunk {
 public:
  RIFFChunk(const std::string& form) : ListTypeChunk("RIFF", form) { }
};

////////////////////////////////////////////////////////////////////////////
// LISTChunk
////////////////////////////////////////////////////////////////////////////
class LISTChunk: public ListTypeChunk {
 public:
  LISTChunk(const std::string& type) : ListTypeChunk("LIST", type) { }
};


////////////////////////////////////////////////////////////////////////////
// RiffFile		-
////////////////////////////////////////////////////////////////////////////
class RiffFile: public RIFFChunk {
 public:
  RiffFile(const std::string& file_name, const std::string& form);

  static void writeLIST(std::vector<u8> &buf, u32 listName, u32 listSize) {
    pushTypeOnVectBE<u32>(buf, 0x4C495354);    //write "LIST"
    pushTypeOnVect<u32>(buf, listSize);
    pushTypeOnVectBE<u32>(buf, listName);
  }

  //Adds a null byte and ensures 16 bit alignment of a text string
  static void alignName(std::string &name) {
    name += '\x00';
    if (name.size() % 2)  //if the size of the name string is odd
      name += '\x00';     //add another null byte
  }


 protected:
  std::string name;
};
/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstring>
#include <cassert>
#include "common.h"

//////////////////////////////////////////////
// Chunk		- Riff format chunk
//////////////////////////////////////////////
class Chunk {
   public:
    char id[4];     //  A chunk ID identifies the type of data within the chunk.
    uint32_t size;  //  The size of the chunk data in bytes, excluding any pad byte.
    uint8_t *data;  //  The actual data not including a possible pad byte to word align

   public:
    Chunk(std::string theId) : data(NULL), size(0) {
        assert(theId.length() == 4);
        memcpy(id, theId.c_str(), 4);
    }
    virtual ~Chunk() {
        if (data != NULL) {
            delete[] data;
            data = NULL;
        }
    }
    void SetData(const void *src, uint32_t datasize);
    virtual uint32_t GetSize();  //  Returns the size of the chunk in bytes, including any pad byte.
    virtual void Write(uint8_t *buffer);

   protected:
    static inline uint32_t GetPaddedSize(uint32_t size) { return size + (size % 2); }
};

////////////////////////////////////////////////////////////////////////////
// ListTypeChunk	- Riff chunk type where the first 4 data bytes are a sig
//					  and the rest of the data is a collection of child chunks
////////////////////////////////////////////////////////////////////////////
class ListTypeChunk : public Chunk {
   public:
    char type[4];  // 4 byte sig that begins the data field, "LIST" or "sfbk" for ex
    std::list<Chunk *> childChunks;

   public:
    ListTypeChunk(std::string theId, std::string theType) : Chunk(theId) {
        assert(theType.length() == 4);
        memcpy(type, theType.c_str(), 4);
    }
    virtual ~ListTypeChunk() { DeleteList(childChunks); }

    Chunk *AddChildChunk(Chunk *ck);
    virtual uint32_t GetSize();  //  Returns the size of the chunk in bytes, including any pad byte.
    virtual void Write(uint8_t *buffer);
};

////////////////////////////////////////////////////////////////////////////
// RIFFChunk
////////////////////////////////////////////////////////////////////////////
class RIFFChunk : public ListTypeChunk {
   public:
    RIFFChunk(std::string form) : ListTypeChunk("RIFF", form) {}
};

////////////////////////////////////////////////////////////////////////////
// LISTChunk
////////////////////////////////////////////////////////////////////////////
class LISTChunk : public ListTypeChunk {
   public:
    LISTChunk(std::string type) : ListTypeChunk("LIST", type) {}
};

////////////////////////////////////////////////////////////////////////////
// RiffFile		-
////////////////////////////////////////////////////////////////////////////
class RiffFile : public RIFFChunk {
   public:
    RiffFile(std::string file_name, std::string form);

    static void WriteLIST(std::vector<uint8_t> &buf, uint32_t listName, uint32_t listSize) {
        PushTypeOnVectBE<uint32_t>(buf, 0x4C495354);  // write "LIST"
        PushTypeOnVect<uint32_t>(buf, listSize);
        PushTypeOnVectBE<uint32_t>(buf, listName);
    }

    // Adds a null byte and ensures 16 bit alignment of a text string
    static void AlignName(std::string &name) {
        name += (char)0x00;
        if (name.size() % 2)     // if the size of the name string is odd
            name += (char)0x00;  // add another null byte
    }

   protected:
    std::string name;
};

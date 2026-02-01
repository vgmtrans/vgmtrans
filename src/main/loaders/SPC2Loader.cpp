/*
 * VGMTrans (c) 2002-2023
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SPC2Loader.h"

#include <cstring>

#include "common.h"
#include "LoaderManager.h"
#include "LogManager.h"

// SPC2 file specs available here: http://blog.kevtris.org/blogfiles/spc2_file_specification_v1.txt

namespace vgmtrans::loaders {
LoaderRegistration<SPC2Loader> _spc2{"SP2"};
}

void SPC2Loader::apply(const RawFile* file) {
  // Constants
  constexpr size_t HEADER_SIZE = 16;
  constexpr size_t SPC_DATA_BLOCK_SIZE = 1024;
  constexpr size_t RAM_BLOCK_SIZE = 256;
  constexpr size_t SPC_HEADER_SIZE = 256;
  constexpr size_t SPC_RAM_SIZE = 65536;
  constexpr size_t SPC_FILE_SIZE =
      SPC_HEADER_SIZE + SPC_RAM_SIZE + 128;  // 256 byte header + 64KB RAM + 128 bytes DSP Registers

  const size_t SPC_FILENAME_OFFSET = 992;
  const size_t SPC_FILENAME_SIZE = 28;

  // File size sanity check. Max ram block size is 16MB, and we'll add a generous 1MB for everything
  // else.
  if (file->size() > (65536 * 256) + (1024 * 1024) || file->size() < HEADER_SIZE) {
    return;
  }

  // Read header
  uint8_t header[HEADER_SIZE];
  file->readBytes(0, HEADER_SIZE, header);

  // Check for header signature. Support major revision 1.
  if (memcmp(header, reinterpret_cast<const void*>("KSPC\x1A\x01"), 6) != 0) {
    return;
  }

  // Extract number of SPCs
  uint16_t numSPCs = header[7] | (header[8] << 8);

  // Iterate over each SPC data block
  for (uint16_t i = 0; i < numSPCs; ++i) {
    // Read SPC data block
    uint8_t spcDataBlock[SPC_DATA_BLOCK_SIZE];
    size_t spcBlockOffset = HEADER_SIZE + (i * SPC_DATA_BLOCK_SIZE);
    file->readBytes(spcBlockOffset, SPC_DATA_BLOCK_SIZE, spcDataBlock);

    // Reconstruct the SPC file's RAM
    auto* spcFile = new uint8_t[SPC_FILE_SIZE];
    memset(spcFile, 0, SPC_FILE_SIZE);

    // Extract spc file name
    // Find the length of the string up to the first null byte or 28 characters, whichever comes
    // first
    const char* dataStart = reinterpret_cast<char*>(spcDataBlock + SPC_FILENAME_OFFSET);
    size_t length = std::find(dataStart, dataStart + SPC_FILENAME_SIZE, '\0') - dataStart;
    std::string originalSpcFilename(dataStart, length);
    originalSpcFilename += ".spc";

    // Construct SPC file header
    const char* headerTitle = "SNES-SPC700 Sound File Data v0.30\x1A\x1A";
    std::memcpy(spcFile, headerTitle, std::strlen(headerTitle));  // Copy header title

    // Copy SPC700 registers from SPC2 file to SPC header
    std::memcpy(spcFile + 0x25, spcDataBlock + 704, 9);  // Copy PC, A, X, Y, PSW, SP

    // Copy the id666 tags
    std::memcpy(spcFile + 0x2E, spcDataBlock + 768, 224);

    // Reconstruct the SPC file's RAM
    for (int j = 0; j < 256; ++j) {
      uint16_t blockIndex = spcDataBlock[j * 2] | (spcDataBlock[j * 2 + 1] << 8);
      uint8_t ramBlock[RAM_BLOCK_SIZE];
      size_t ramBlockOffset = 16 + (numSPCs * SPC_DATA_BLOCK_SIZE) + (blockIndex * RAM_BLOCK_SIZE);
      file->readBytes(ramBlockOffset, RAM_BLOCK_SIZE, ramBlock);
      std::copy_n(ramBlock, RAM_BLOCK_SIZE, spcFile + SPC_HEADER_SIZE + (j * RAM_BLOCK_SIZE));
    }

    // Add DSP Registers
    std::copy(spcDataBlock + 512, spcDataBlock + 640, spcFile + SPC_HEADER_SIZE + SPC_RAM_SIZE);

    // Save the reconstructed SPC file
    auto virtFile = new VirtFile(spcFile, SPC_FILE_SIZE, originalSpcFilename, "", file->tag);
    enqueue(virtFile);
  }
}

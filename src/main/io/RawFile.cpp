/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RawFile.h"

#include "base/Types.h"
#include "components/VGMFile.h"
#include "LogManager.h"
#include "util/BytePattern.h"

/* RawFile */

void RawFile::addContainedVGMFile(VGMFileVariant vgmfile) {
    m_vgmfiles.emplace_back(vgmfile);
}

void RawFile::removeContainedVGMFile(VGMFileVariant vgmfile) {
    auto iter = std::ranges::find(m_vgmfiles, vgmfile);
    if (iter != m_vgmfiles.end())
        m_vgmfiles.erase(iter);
    else {
        L_WARN("Requested deletion for VGMFile but it was not found");
    }
}

u32 RawFile::readBytes(size_t offset, u32 nCount, void *pBuffer) const {
    memcpy(pBuffer, data() + offset, nCount);
    return nCount;
}

bool RawFile::matchBytes(const u8 *pattern, size_t offset, size_t nCount) const {
    return memcmp(data() + offset, pattern, nCount) == 0;
}

bool RawFile::matchBytePattern(const BytePattern &pattern, size_t offset) const {
    return pattern.match(data() + offset, pattern.length());
}

bool RawFile::searchBytePattern(const BytePattern &pattern, u32 &nMatchOffset,
                                u32 nSearchOffset, u32 nSearchSize) const {
    if (nSearchOffset >= size())
        return false;

    if ((nSearchOffset + nSearchSize) > size())
        nSearchSize = size() - nSearchOffset;

    if (nSearchSize < pattern.length())
        return false;

    for (size_t offset = nSearchOffset; offset < nSearchOffset + nSearchSize - pattern.length();
         offset++) {
        if (matchBytePattern(pattern, offset)) {
            nMatchOffset = offset;
            return true;
        }
    }
    return false;
}

std::string RawFile::readNullTerminatedString(size_t offset, size_t maxLength) const {
  const char* stringPtr = data() + offset;
  size_t length = strnlen(stringPtr, maxLength);
  return std::string(stringPtr, length);
}

/* DiskFile */

DiskFile::DiskFile(const std::filesystem::path& path) : m_data(mio::mmap_source(path.c_str())), m_path(path) {}

/* VirtFile */

VirtFile::VirtFile(const RawFile &file, size_t offset) : m_name(file.name()), m_lpath(file.path()) {
    std::copy(file.begin() + offset, file.end(), std::back_inserter(m_data));
}

VirtFile::VirtFile(const RawFile &file, size_t offset, size_t limit)
    : m_name(file.name()), m_lpath(file.path()) {
    std::copy_n(file.data() + offset, limit, std::back_inserter(m_data));
}

VirtFile::VirtFile(const u8 *data, u32 fileSize, std::string name,
                   std::filesystem::path parent_fullpath, const VGMTag& tag)
    : m_name(std::move(name)), m_lpath(std::move(parent_fullpath)) {
  std::copy_n(data, fileSize, std::back_inserter(m_data));
  this->tag = tag;
}

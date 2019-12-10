/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFile.h"
#include "Root.h"

/* RawFile */

RawFile::~RawFile(void) {
    pRoot->UI_BeginRemoveVGMFiles();
    size_t size = containedVGMFiles.size();
    for (size_t i = 0; i < size; i++) {
        pRoot->RemoveVGMFile(containedVGMFiles.front(), false);
        containedVGMFiles.erase(containedVGMFiles.begin());
    }

    pRoot->UI_EndRemoveVGMFiles();
}

std::wstring RawFile::removeExtFromPath(const std::wstring &s) {
    size_t i = s.rfind('.', s.length());
    if (i != std::wstring::npos) {
        return (s.substr(0, i));
    }

    return s;
}

/* Get the item at the specified offset */
VGMItem *RawFile::GetItemFromOffset(long offset) {
    for (auto file : containedVGMFiles) {
        auto item = file->GetItemFromOffset(offset);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

/* Get the VGMFile at the specified offset */
VGMFile *RawFile::GetVGMFileFromOffset(long offset) {
    for (auto file : containedVGMFiles) {
        if (file->IsItemAtOffset(offset)) {
            return file;
        }
    }

    return nullptr;
}

/* FIXME: we own the VGMFile, should use unique_ptr instead */
void RawFile::AddContainedVGMFile(VGMFile *vgmfile) {
    containedVGMFiles.push_back(vgmfile);
}

void RawFile::RemoveContainedVGMFile(VGMFile *vgmfile) {
    auto iter = find(containedVGMFiles.begin(), containedVGMFiles.end(), vgmfile);
    if (iter != containedVGMFiles.end())
        containedVGMFiles.erase(iter);
    else
        L_WARN("Requested deletion for VGMFile '{}' but it was not found",
               wstring2string(*const_cast<std::wstring *>(vgmfile->GetName())));

    if (containedVGMFiles.size() == 0)
        pRoot->CloseRawFile(this);
}

uint32_t RawFile::GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) {
    memcpy(pBuffer, data() + nIndex, nCount);
    return nCount;
}

bool RawFile::MatchBytes(const uint8_t *pattern, uint32_t nIndex, size_t nCount) {
    return memcmp(data() + nIndex, pattern, nCount) == 0;
}

bool RawFile::MatchBytePattern(const BytePattern &pattern, uint32_t nIndex) {
    return pattern.match(data() + nIndex, pattern.length());
}

bool RawFile::SearchBytePattern(const BytePattern &pattern, uint32_t &nMatchOffset,
                                uint32_t nSearchOffset, uint32_t nSearchSize) {
    if (nSearchOffset >= size())
        return false;

    if ((nSearchOffset + nSearchSize) > size())
        nSearchSize = size() - nSearchOffset;

    if (nSearchSize < pattern.length())
        return false;

    for (uint32_t nIndex = nSearchOffset; nIndex < nSearchOffset + nSearchSize - pattern.length();
         nIndex++) {
        if (MatchBytePattern(pattern, nIndex)) {
            nMatchOffset = nIndex;
            return true;
        }
    }
    return false;
}

bool RawFile::OnSaveAsRaw() {
    std::wstring filepath = pRoot->UI_GetSaveFilePath(L"");
    if (!filepath.empty()) {
        uint8_t *buf = new uint8_t[size()];
        GetBytes(0, size(), buf);
        bool result = pRoot->UI_WriteBufferToFile(filepath, buf, size());
        delete[] buf;
        return result;
    }
    return false;
}

/* DiskFile */

DiskFile::DiskFile(const std::wstring &path) : m_data(mio::mmap_source(path)), m_path(path) {}

DiskFile::DiskFile(std::string_view path) : m_data(mio::mmap_source(path)), m_path(path) {}

/* VirtFile */

VirtFile::VirtFile(const RawFile &file, size_t offset) : m_name(file.name()), m_lpath(file.path()) {
    std::copy(file.data() + offset, file.data() + offset + file.size(), std::back_inserter(m_data));
}

VirtFile::VirtFile(uint8_t *data, uint32_t fileSize, std::wstring name,
                   std::wstring parent_fullpath, const VGMTag tag)
    : m_name(std::move(name)), m_lpath(std::move(parent_fullpath)) {
    std::copy(data, data + fileSize, std::back_inserter(m_data));
}

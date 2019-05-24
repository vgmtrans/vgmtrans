/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFile.h"
#include "Root.h"

using namespace std;

#define BUF_SIZE 0x100000  // 1mb

RawFile::RawFile(void)
    : propreRatio(0.5),
      processFlags(PF_USESCANNERS | PF_USELOADERS),
      parRawFileFullPath(L""),
      bCanFileRead(true),
      fileSize(0) {
    bufSize = (BUF_SIZE > fileSize) ? fileSize : BUF_SIZE;
}

RawFile::RawFile(const wstring name, uint32_t theFileSize, bool bCanRead, const VGMTag tag)
    : fileSize(theFileSize),
      fullpath(name),
      parRawFileFullPath(L""),  // this should only be defined by VirtFile
      propreRatio(0.5),
      bCanFileRead(bCanRead),
      tag(tag),
      processFlags(PF_USESCANNERS | PF_USELOADERS) {
    filename = getFileNameFromPath(fullpath);
    extension = getExtFromPath(fullpath);
}

RawFile::~RawFile(void) {
    pRoot->UI_BeginRemoveVGMFiles();
    size_t size = containedVGMFiles.size();
    for (size_t i = 0; i < size; i++) {
        pRoot->RemoveVGMFile(containedVGMFiles.front(), false);
        containedVGMFiles.erase(containedVGMFiles.begin());
    }

    pRoot->UI_EndRemoveVGMFiles();
    pRoot->UI_CloseRawFile(this);
}

// opens a file using the standard c++ file i/o routines
bool RawFile::open(const wstring &theFileName) {
#if _MSC_VER < 1400  // if we're not using VC8, and the new STL that supports widechar filenames in
                     // ofstream...

    char newpath[256];
    wcstombs(newpath, theFileName.c_str(), 256);
    file.open(newpath, ios::in | ios::binary);
#else
    file.open(theFileName, ios::in | ios::binary);
#endif
    if (!file.is_open()) {
        L_ERROR("The file '{}' could not be opened", newpath);
        return false;
    }

    // get pointer to associated buffer object
    pbuf = file.rdbuf();

    // get file size using buffer's members
    fileSize = (uint32_t)pbuf->pubseekoff(0, ios::end, ios::in);

    bufSize = (BUF_SIZE > fileSize) ? fileSize : BUF_SIZE;
    buf.alloc(bufSize);
    m_data = std::make_unique<DataBlock>(0, bufSize);
    return true;
}

// closes the file
void RawFile::close() {
    file.close();
}

// returns the size of the file
unsigned long RawFile::size(void) {
    return fileSize;
}

// Name says it all.
wstring RawFile::getFileNameFromPath(const wstring &s) {
    size_t i = s.rfind('/', s.length());
    size_t j = s.rfind('\\', s.length());
    if (i == string::npos || (j != string::npos && i < j))
        i = j;
    if (i != string::npos) {
        return (s.substr(i + 1, s.length() - i));
    }
    return s;
}

wstring RawFile::getExtFromPath(const wstring &s) {
    size_t i = s.rfind('.', s.length());
    if (i != string::npos) {
        return (s.substr(i + 1, s.length() - i));
    }
    return (L"");
}

wstring RawFile::removeExtFromPath(const wstring &s) {
    size_t i = s.rfind('.', s.length());
    if (i != string::npos) {
        return (s.substr(0, i));
    }
    return s;
}

// Sets the  "ProPre" ratio.  Terrible name, yes.  Every time the buffer is updated
// from an attempt to read at an offset beyond its current bounds, the propre ratio determines
// the new bounds of the buffer.  Literally, it is a ratio determining how much of the bounds should
// be ahead of the current offset.  If, for example, the ratio were 0.7, 70% of the buffer would
// contain data ahead of the requested offset and 30% would be below it.
void RawFile::SetProPreRatio(float newRatio) {
    if (newRatio > 1 || newRatio < 0)
        return;
    propreRatio = newRatio;
}

// given an offset, determines which, if any, VGMItem is encompasses the offset
VGMItem *RawFile::GetItemFromOffset(long offset) {
    for (list<VGMFile *>::iterator iter = containedVGMFiles.begin();
         iter != containedVGMFiles.end(); iter++) {
        VGMItem *item = (*iter)->GetItemFromOffset(offset);
        if (item != NULL)
            return item;
    }
    return NULL;
}

// given an offset, determines which, if any, VGMFile encompasses the offset.
VGMFile *RawFile::GetVGMFileFromOffset(long offset) {
    for (list<VGMFile *>::iterator iter = containedVGMFiles.begin();
         iter != containedVGMFiles.end(); iter++) {
        if ((*iter)->IsItemAtOffset(offset))
            return *iter;
    }
    return NULL;
}

// adds the association of a VGMFile to this RawFile
void RawFile::AddContainedVGMFile(VGMFile *vgmfile) {
    containedVGMFiles.push_back(vgmfile);
}

// removes the association of a VGMFile with this RawFile
void RawFile::RemoveContainedVGMFile(VGMFile *vgmfile) {
    list<VGMFile *>::iterator iter =
        find(containedVGMFiles.begin(), containedVGMFiles.end(), vgmfile);
    if (iter != containedVGMFiles.end())
        containedVGMFiles.erase(iter);
    else
        L_WARN("Requested deletion for VGMFile '{}' but it was not found",
               wstring2string(*const_cast<std::wstring *>(vgmfile->GetName())));

    if (containedVGMFiles.size() == 0)
        pRoot->CloseRawFile(this);
}

// read data directly from the file
int RawFile::FileRead(void *dest, uint32_t index, uint32_t length) {
    assert(bCanFileRead);
    assert(index + length <= fileSize);
    pbuf->pubseekpos(index, ios::in);
    return (int)pbuf->sgetn(
        (char *)dest, length);  // return bool value based on whether we read all requested bytes
}

// reads a bunch of data from a given offset.  If the requested data goes beyond the bounds
// of the file buffer, the buffer is updated.  If the requested size is greater than the buffer size
// a direct read from the file is executed.
uint32_t RawFile::GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) {
    if ((nIndex + nCount) > fileSize)
        nCount = fileSize - nIndex;

    if (nCount > m_data->size())
        FileRead(pBuffer, nIndex, nCount);
    else {
        if ((nIndex < m_data->beginOffset()) || (nIndex + nCount > m_data->endOffset()))
            UpdateBuffer(nIndex);

        memcpy(pBuffer, m_data->data() + nIndex - m_data->beginOffset(), nCount);
    }
    return nCount;
}

// attempts to match the data from a given offset against a given pattern.
// If the requested data goes beyond the bounds of the file buffer, the buffer is updated.
// If the requested size is greater than the buffer size, it always fails. (operation not supported)
bool RawFile::MatchBytes(const uint8_t *pattern, uint32_t nIndex, size_t nCount) {
    if ((nIndex + nCount) > fileSize)
        return false;

    if (nCount > m_data->size())
        return false;  // not supported
    else {
        if ((nIndex < m_data->beginOffset()) || (nIndex + nCount > m_data->endOffset()))
            UpdateBuffer(nIndex);

        return (memcmp(m_data->data() + nIndex - m_data->beginOffset(), pattern, nCount) == 0);
    }
}

// attempts to match the data from a given offset against a given pattern.
// If the requested data goes beyond the bounds of the file buffer, the buffer is updated.
// If the requested size is greater than the buffer size, it always fails. (operation not supported)
bool RawFile::MatchBytePattern(const BytePattern &pattern, uint32_t nIndex) {
    size_t nCount = pattern.length();

    if ((nIndex + nCount) > fileSize)
        return false;

    if (nCount > m_data->size())
        return false;  // not supported
    else {
        if ((nIndex < m_data->beginOffset()) || (nIndex + nCount > m_data->endOffset()))
            UpdateBuffer(nIndex);

        return pattern.match(m_data->data() + nIndex - m_data->beginOffset(), nCount);
    }
}

bool RawFile::SearchBytePattern(const BytePattern &pattern, uint32_t &nMatchOffset,
                                uint32_t nSearchOffset, uint32_t nSearchSize) {
    if (nSearchOffset >= fileSize)
        return false;

    if ((nSearchOffset + nSearchSize) > fileSize)
        nSearchSize = fileSize - nSearchOffset;

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

// Given a requested offset, fills the buffer with data surstd::roundng that offset.  The ratio
// of how much data is read before and after the offset is determined by the ProPre ratio (explained
// above).
void RawFile::UpdateBuffer(uint32_t index) {
    uint32_t beginOffset = 0;
    uint32_t endOffset = 0;

    assert(bCanFileRead);

    if (!buf.bAlloced)
        buf.alloc(bufSize);

    uint32_t proBytes = (uint32_t)(m_data->size() * propreRatio);
    uint32_t preBytes = m_data->size() - proBytes;
    if (proBytes + index > fileSize) {
        preBytes += (proBytes + index) - fileSize;
        proBytes = fileSize - index;  // make it go just to the end of the file;
    } else if (preBytes > index) {
        proBytes += preBytes - index;
        preBytes = index;
    }
    beginOffset = index - preBytes;
    endOffset = index + proBytes;

    if (beginOffset >= m_data->beginOffset() && beginOffset < m_data->endOffset()) {
        uint32_t prevEndOff = m_data->endOffset();
        m_data->relocate(beginOffset);
        FileRead(m_data->data() + prevEndOff - m_data->beginOffset(), prevEndOff,
                 endOffset - prevEndOff);
    } else if (endOffset >= m_data->beginOffset() && endOffset < m_data->endOffset()) {
        uint32_t prevStartOff = m_data->beginOffset();
        m_data->relocate(beginOffset);
        FileRead(m_data->data(), beginOffset, prevStartOff - beginOffset);
    } else {
        FileRead(m_data->data(), beginOffset, m_data->size());
        m_data->setBeginOffset(beginOffset);
        m_data->setEndOffset(beginOffset + m_data->size());
    }
}

bool RawFile::OnSaveAsRaw() {
    wstring filepath = pRoot->UI_GetSaveFilePath(ConvertToSafeFileName(filename));
    if (filepath.length() != 0) {
        bool result;
        uint8_t *buf = new uint8_t[fileSize];  // create a buffer the size of the file
        GetBytes(0, fileSize, buf);
        result = pRoot->UI_WriteBufferToFile(filepath, buf, fileSize);
        delete[] buf;
        return result;
    }
    return false;
}

//  ********
//  VirtFile
//  ********

VirtFile::VirtFile() : RawFile() {}

VirtFile::VirtFile(uint8_t *data, uint32_t fileSize, const wstring &name,
                   const wchar_t *rawFileName, const VGMTag tag)
    : RawFile(name, fileSize, false, tag) {
    parRawFileFullPath = rawFileName;
    m_data = std::make_unique<DataBlock>(data, 0, fileSize);
    // buf.insert_back(data, fileSize);
}
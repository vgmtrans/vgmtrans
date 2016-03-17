#include "pch.h"

#include "common.h"
#include "Root.h"
#include "VGMFile.h"
#include "Format.h"

DECLARE_MENU(VGMFile)

using namespace std;

VGMFile::VGMFile(FileType fileType,
                 const string &fmt,
                 RawFile *theRawFile,
                 uint32_t offset,
                 uint32_t length,
                 wstring theName)
    : VGMContainerItem(this, offset, length),
      rawfile(theRawFile),
      bUsingRawFile(true),
      bUsingCompressedLocalData(false),
      format(fmt),
      file_type(fileType),
      name(theName),
      id(-1) {
}

VGMFile::~VGMFile(void) {

}

// Only difference between this AddToUI and VGMItemContainer's version is that we do not add
// this as an item because we do not want the VGMFile to be itself an item in the Item View
void VGMFile::AddToUI(VGMItem *parent, void *UI_specific) {
  for (uint32_t i = 0; i < containers.size(); i++) {
    for (uint32_t j = 0; j < containers[i]->size(); j++)
      (*containers[i])[j]->AddToUI(this, UI_specific);
  }
}

bool VGMFile::OnClose() {
  pRoot->RemoveVGMFile(this);
  return true;
}

bool VGMFile::OnSaveAsRaw() {
  wstring filepath = pRoot->UI_GetSaveFilePath(ConvertToSafeFileName(name));
  if (filepath.length() != 0) {
    bool result;
    uint8_t *buf = new uint8_t[unLength];        //create a buffer the size of the file
    GetBytes(dwOffset, unLength, buf);
    result = pRoot->UI_WriteBufferToFile(filepath, buf, unLength);
    delete[] buf;
    return result;
  }
  return false;
}

bool VGMFile::OnSaveAllAsRaw() {
  return pRoot->SaveAllAsRaw();
}

bool VGMFile::LoadVGMFile() {
  bool val = Load();
  if (!val)
    return false;
  Format *fmt = GetFormat();
  if (fmt)
    fmt->OnNewFile(this);

  pRoot->AddLogItem(new LogItem(wstring(L"Loaded \"" + name + L"\" successfully.").c_str(),
                                LOG_LEVEL_INFO,
                                L"VGMFile"));
  return val;
}

Format *VGMFile::GetFormat() {
  return Format::GetFormatFromName(format);
}

const string &VGMFile::GetFormatName() {
  return format;
}


const wstring *VGMFile::GetName(void) const {
  return &name;
}

void VGMFile::AddCollAssoc(VGMColl *coll) {
  assocColls.push_back(coll);
}

void VGMFile::RemoveCollAssoc(VGMColl *coll) {
  list<VGMColl *>::iterator iter = find(assocColls.begin(), assocColls.end(), coll);
  if (iter != assocColls.end())
    assocColls.erase(iter);
}

//These functions are common to all VGMItems, but no reason to refer to vgmfile
//or call GetRawFile() if the item itself is a VGMFile
RawFile *VGMFile::GetRawFile() {
  return rawfile;
}

void VGMFile::LoadLocalData() {
  assert(unLength < 1024 * 1024 * 256); //sanity check... we're probably not allocating more than 256mb
  data.clear();
  data.alloc(unLength);
  rawfile->GetBytes(dwOffset, unLength, data.data);
  data.startOff = dwOffset;
  data.endOff = dwOffset + unLength;
  data.size = unLength;
}

void VGMFile::UseLocalData() {
  bUsingRawFile = false;
}

void VGMFile::UseRawFileData() {
  bUsingRawFile = true;
}


uint32_t VGMFile::GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) {
  // if unLength != 0, verify that we're within the bounds of the file, and truncate num read bytes to end of file
  if (unLength != 0) {
    uint32_t endOff = dwOffset + unLength;
    assert (nIndex >= dwOffset && nIndex < endOff);
    if (nIndex + nCount > endOff)
      nCount = endOff - nIndex;
  }

  if (bUsingRawFile)
    return rawfile->GetBytes(nIndex, nCount, pBuffer);
  else {
    if ((nIndex + nCount) > data.endOff)
      nCount = data.endOff - nIndex;

    assert(nIndex >= data.startOff && (nIndex + nCount <= data.endOff));
    memcpy(pBuffer, data.data + nIndex - data.startOff, nCount);
    return nCount;
  }
}



// *********
// VGMHeader
// *********

VGMHeader::VGMHeader(VGMItem *parItem, uint32_t offset, uint32_t length, const std::wstring &name)
    : VGMContainerItem(parItem->vgmfile, offset, length, name) {
}

VGMHeader::~VGMHeader() {
}

void VGMHeader::AddPointer(uint32_t offset,
                           uint32_t length,
                           uint32_t destAddress,
                           bool notNull,
                           const std::wstring &name) {
  localitems.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_POINTER, offset, length, name));
}

void VGMHeader::AddTempo(uint32_t offset, uint32_t length, const std::wstring &name) {
  localitems.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_TEMPO, offset, length, name));
}

void VGMHeader::AddSig(uint32_t offset, uint32_t length, const std::wstring &name) {
  localitems.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_SIG, offset, length, name));
}

// *************
// VGMHeaderItem
// *************

VGMHeaderItem::VGMHeaderItem(VGMHeader *hdr,
                             HdrItemType theType,
                             uint32_t offset,
                             uint32_t length,
                             const std::wstring &name)
    : VGMItem(hdr->vgmfile, offset, length, name, CLR_HEADER), type(theType) {
}

VGMItem::Icon VGMHeaderItem::GetIcon() {
  switch (type) {
    case HIT_UNKNOWN:
      return ICON_UNKNOWN;
    case HIT_POINTER:
      return ICON_BINARY;
    case HIT_TEMPO:
      return ICON_TEMPO;
    case HIT_SIG:
      return ICON_BINARY;
    default:
      return ICON_BINARY;
  }
}
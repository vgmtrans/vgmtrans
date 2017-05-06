#include "pch.h"
#include "VGMMiscFile.h"
#include "main/Core.h"

using namespace std;

// ***********
// VGMMiscFile
// ***********

VGMMiscFile::VGMMiscFile(const string &format, RawFile *file, uint32_t offset, uint32_t length, wstring name)
    : VGMFile(FILETYPE_MISC, format, file, offset, length, name) {

}

bool VGMMiscFile::LoadMain() {
  return true;
}

bool VGMMiscFile::Load() {
  if (!LoadMain())
    return false;
  if (unLength == 0)
    return false;

  LoadLocalData();
  UseLocalData();
  core.AddVGMFile(this);
  return true;
}

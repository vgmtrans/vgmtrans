#pragma once

#include <variant>
class VGMFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

using VGMFileVariant = std::variant<VGMSeq*, VGMInstrSet*, VGMSampColl*, VGMMiscFile*>;

template <typename T>
T* variantToType(VGMFileVariant variant) {
  T* vgmFilePtr = nullptr;
  std::visit([&vgmFilePtr](auto* vgm) { vgmFilePtr = dynamic_cast<T*>(vgm); }, variant);
  return vgmFilePtr;
}

VGMFile* variantToVGMFile(VGMFileVariant variant);
VGMFileVariant vgmFileToVariant(VGMFile* vgmfile);

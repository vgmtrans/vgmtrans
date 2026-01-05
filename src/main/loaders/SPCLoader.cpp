/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SPCLoader.h"
#include "LogManager.h"
#include "LoaderManager.h"
#include "SPCFile.h"

namespace vgmtrans::loaders {
LoaderRegistration<SPCLoader> _spc{"SPC"};
}

void SPCLoader::apply(const RawFile *file) {
  if (file->size() < 0x10180) {
    return;
  }

  if (!std::equal(file->begin(), file->begin() + 27, "SNES-SPC700 Sound File Data") ||
      file->readShort(0x21) != 0x1a1a) {
    return;
  }

  try {
    SPCFile spc(*file);

    auto tag = SPCFile::tagFromSPCFile(spc);
    std::string name = fmt::format("{} - ram", file->name());
    auto newfile = new VirtFile(spc.ram().data(), spc.ram().size(), name, file->path(), tag);
    enqueue(newfile);
  } catch (std::exception e) {
    L_ERROR(e.what());
  }
}

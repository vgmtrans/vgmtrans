/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NDS2SFLoader.h"

#include <filesystem>

#include "PSFFile2.h"
#include <fmt/compile.h>
#include <fmt/format.h>

int constexpr NDS2SF_VERSION = 0x24;

void NDS2SFLoader::apply(const RawFile *file) {
    uint8_t sig[4];
    file->GetBytes(0, 4, sig);
    if (memcmp(sig, "PSF", 3) == 0) {
        uint8_t version = sig[3];
        if (version == NDS2SF_VERSION) {
            psf_read_exe(file);
        }
    }
}

void NDS2SFLoader::psf_read_exe(const RawFile *file) {
    PSFFile2 psf(*file);

    std::filesystem::path basepath(file->path());
    auto libtag = psf.tags().find("_lib");
    if (libtag != psf.tags().end()) {
        auto newpath = basepath.replace_filename(libtag->second).string();
        auto newfile = std::make_shared<DiskFile>(newpath);
        enqueue(newfile);

        /* Look for additional libraries in the same folder */
        auto libstring = fmt::compile("_lib{:d}");
        int i = 1;
        while (true) {
            libtag = psf.tags().find(fmt::format(libstring, i++));
            if (libtag != psf.tags().end()) {
                newpath = basepath.replace_filename(libtag->second).string();
                newfile = std::make_shared<DiskFile>(newpath);
                enqueue(newfile);
            } else {
                break;
            }
        }
    }

    auto romsize = psf.getExe<u32>(0x04);
    auto newfile =
        std::make_shared<VirtFile>(reinterpret_cast<const u8 *>(psf.exe().data()) + 0x8,
                                   psf.exe().size(), file->name(), file->path(), file->tag);
    enqueue(newfile);
}

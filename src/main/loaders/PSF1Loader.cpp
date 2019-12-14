/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSF1Loader.h"
#include "Root.h"

#include "PSFFile2.h"
#include <fmt/compile.h>
#include <fmt/format.h>

void PSF1Loader::apply(const RawFile *file) {
    uint8_t sig[4];
    file->GetBytes(0, 4, sig);
    if (memcmp(sig, "PSF", 3) == 0) {
        uint8_t version = sig[3];
        if (version == 0x01) {
            psf_read_exe(file);
        }
    }
}

void PSF1Loader::psf_read_exe(const RawFile *file) {
    PSFFile2 psf(*file);
    auto out = new std::vector<char>();

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

    auto newfile =
        std::make_shared<VirtFile>(reinterpret_cast<const u8 *>(psf.exe().data()) + 0x800,
                                   psf.exe().size() - 0x800, file->name(), file->path(), file->tag);
    enqueue(newfile);
}

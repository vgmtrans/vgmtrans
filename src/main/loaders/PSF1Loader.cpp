/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSF1Loader.h"
#include "Root.h"

#include "PSFFile.h"

PostLoadCommand PSF1Loader::Apply(RawFile *file) {
    uint8_t sig[4];
    file->GetBytes(0, 4, sig);
    if (memcmp(sig, "PSF", 3) == 0) {
        uint8_t version = sig[3];
        if (version == 0x01) {
            auto data = psf_read_exe(file);
            if (data->empty()) {
                return KEEP_IT;
            }

            std::wstring name = file->name();
            pRoot->CreateVirtFile(reinterpret_cast<u8 *>(data->data()), data->size(), name.data(),
                                  L"", file->tag);
            return DELETE_IT;
        }
    }

    return KEEP_IT;
}

std::vector<char> *PSF1Loader::psf_read_exe(RawFile *file) {
    PSFFile2 psf(file);
    auto out = new std::vector<char>();

    size_t stext_ofs = psf.getExe<u32>(0x18) & 0x3FFFFF;
    size_t stext_size = psf.getExe<u32>(0x1C);

    auto loaded_libs = load_psf_libs(psf, file);
    if (!loaded_libs)
        return out;

    auto exeb = psf.exe().cbegin() + 0x800;
    std::copy(exeb, exeb + stext_size, std::back_inserter(*out));

    return out;
}

bool PSF1Loader::load_psf_libs(PSFFile2 &psf, RawFile *file) {
    char libTagName[16];
    int libIndex = 1;
    while (true) {
        if (libIndex == 1)
            strcpy(libTagName, "_lib");
        else
            sprintf(libTagName, "_lib%d", libIndex);

        auto itLibTag = psf.tags().find(libTagName);
        if (itLibTag == psf.tags().end())
            break;

        wchar_t tempfn[PATH_MAX] = {0};
        mbstowcs(tempfn, itLibTag->second.c_str(), itLibTag->second.size());

        wchar_t *fullPath;
        fullPath = GetFileWithBase(file->path().c_str(), tempfn);

        DiskFile *newRawFile = new DiskFile(fullPath);
        std::vector<char> *out = nullptr;
        out = psf_read_exe(newRawFile);
        delete fullPath;
        delete newRawFile;

        if (out && out->empty())
            return false;

        libIndex++;
    }

    return true;
}

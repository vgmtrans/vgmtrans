/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <filesystem>
#include "NDS2SFLoader.h"
#include "Root.h"

using namespace std;

#define NDS2SF_VERSION 0x24
#define NDS2SF_MAX_ROM_SIZE 0x10000000

char *GetFileWithBase(const char *f, const char *newfile);

NDS2SFLoader::NDS2SFLoader(void) {}

NDS2SFLoader::~NDS2SFLoader(void) {}

PostLoadCommand NDS2SFLoader::Apply(RawFile *file) {
    uint8_t sig[4];
    file->GetBytes(0, 4, sig);
    if (memcmp(sig, "PSF", 3) == 0) {
        uint8_t version = sig[3];
        if (version == NDS2SF_VERSION) {
            size_t exebufsize = NDS2SF_MAX_ROM_SIZE;
            uint8_t *exebuf = NULL;

            auto complaint = psf_read_exe(file, exebuf, exebufsize);
            if (complaint) {
                L_ERROR("{}", (std::string{complaint}));
                delete[] exebuf;
                return KEEP_IT;
            }

            string str = file->name();
            pRoot->CreateVirtFile(exebuf, (uint32_t)exebufsize, str.data(), "", file->tag);
            return DELETE_IT;
        }
    }

    return KEEP_IT;
}

// The code below was written by Neill Corlett and adapted for VGMTrans.
// Recursive psflib loading has been added.

/***************************************************************************/
/*
** Read the EXE from a PSF file
**
** Returns the error message, or NULL on success
*/
const char *NDS2SFLoader::psf_read_exe(RawFile *file, unsigned char *&exebuffer,
                                       size_t &exebuffersize) {
    PSFFile psf;
    if (!psf.Load(file))
        return psf.GetError();

    // search exclusively for _lib tag, and if found, perform a recursive load
    const char *psflibError = load_psf_libs(psf, file, exebuffer, exebuffersize);
    if (psflibError != NULL)
        return psflibError;

    DataSeg *nds2sfExeHeadSeg;
    if (!psf.ReadExeDataSeg(nds2sfExeHeadSeg, 0x08, 0))
        return psf.GetError();

    uint32_t nds2sfRomStart = nds2sfExeHeadSeg->GetWord(0x00);
    uint32_t nds2sfRomSize = nds2sfExeHeadSeg->GetWord(0x04);
    delete nds2sfExeHeadSeg;
    if (nds2sfRomStart + nds2sfRomSize > exebuffersize || (exebuffer == NULL && exebuffersize == 0))
        return "2SF ROM section start and/or size values are likely corrupt.";

    if (exebuffer == NULL) {
        exebuffersize = nds2sfRomStart + nds2sfRomSize;
        exebuffer = new uint8_t[exebuffersize];
        if (exebuffer == NULL) {
            return "2SF ROM memory allocation error.";
        }
        memset(exebuffer, 0, exebuffersize);
    }

    if (!psf.ReadExe(exebuffer + nds2sfRomStart, nds2sfRomSize, 0x08))
        return "Decompression failed";

    // set tags to RawFile
    if (psf.tags.count("title") != 0) {
        file->tag.title = (psf.tags["title"]);
    }
    if (psf.tags.count("artist") != 0) {
        file->tag.artist = (psf.tags["artist"]);
    }
    if (psf.tags.count("game") != 0) {
        file->tag.album = (psf.tags["game"]);
    }
    if (psf.tags.count("comment") != 0) {
        file->tag.comment = (psf.tags["comment"]);
    }

    return NULL;
}

const char *NDS2SFLoader::load_psf_libs(PSFFile &psf, RawFile *file, unsigned char *&exebuffer,
                                        size_t &exebuffersize) {
    char libTagName[16];
    int libIndex = 1;
    while (true) {
        if (libIndex == 1)
            strcpy(libTagName, "_lib");
        else
            sprintf(libTagName, "_lib%d", libIndex);

        auto itLibTag = psf.tags.find(libTagName);
        if (itLibTag == psf.tags.end())
            break;

        std::string newpath =
            std::filesystem::path{file->path()}.replace_filename(itLibTag->second).string();

        // TODO: Make sure to limit recursion to avoid crashing.
        DiskFile *newRawFile = new DiskFile(newpath);
        const char *psflibError = NULL;
        psflibError = psf_read_exe(newRawFile, exebuffer, exebuffersize);
        delete newRawFile;

        if (psflibError != NULL)
            return psflibError;

        libIndex++;
    }
    return NULL;
}

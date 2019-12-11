/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "GSFLoader.h"
#include "Root.h"

using namespace std;

#define GSF_VERSION 0x22
#define GSF_MAX_ROM_SIZE 0x2000000

char *GetFileWithBase(const char *f, const char *newfile);

GSFLoader::GSFLoader(void) {}

GSFLoader::~GSFLoader(void) {}

PostLoadCommand GSFLoader::Apply(RawFile *file) {
    uint8_t sig[4];
    file->GetBytes(0, 4, sig);
    if (memcmp(sig, "PSF", 3) == 0) {
        uint8_t version = sig[3];
        if (version == GSF_VERSION) {
            std::string complaint;
            size_t exebufsize = GSF_MAX_ROM_SIZE;
            uint8_t *exebuf = NULL;
            // memset(exebuf, 0, exebufsize);

            complaint = std::string{psf_read_exe(file, exebuf, exebufsize)};
            if (!complaint.empty()) {
                L_ERROR("{}", (complaint));
                delete[] exebuf;
                return KEEP_IT;
            }
            // pRoot->UI_WriteBufferToFile("uncomp.gba", exebuf, exebufsize);

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
const char *GSFLoader::psf_read_exe(RawFile *file, unsigned char *&exebuffer,
                                    size_t &exebuffersize) {
    PSFFile psf;
    if (!psf.Load(file))
        return psf.GetError();

    // search exclusively for _lib tag, and if found, perform a recursive load
    const char *psflibError = load_psf_libs(psf, file, exebuffer, exebuffersize);
    if (psflibError != NULL)
        return psflibError;

    DataSeg *gsfExeHeadSeg;
    if (!psf.ReadExeDataSeg(gsfExeHeadSeg, 0x0c, 0))
        return psf.GetError();

    // uint32_t gsfEntryPoint = gsfExeHeadSeg->GetWord(0x00);
    uint32_t gsfRomStart = gsfExeHeadSeg->GetWord(0x04) & 0x01FFFFFF;
    uint32_t gsfRomRegion = gsfExeHeadSeg->GetWord(0x04) & 0xFE000000;
    uint32_t gsfRomSize = gsfExeHeadSeg->GetWord(0x08);
    delete gsfExeHeadSeg;
    if (gsfRomRegion != 0x08000000)
        return "GSF ROM offset points to unsupported region. (multi-boot GSF is not supported "
               "yet)";
    if (gsfRomStart + gsfRomSize > exebuffersize || (exebuffer == NULL && exebuffersize == 0))
        return "GSF ROM section start and/or size values are corrupt.";

    if (exebuffer == NULL) {
        exebuffersize = gsfRomStart + gsfRomSize;
        exebuffer = new uint8_t[exebuffersize];
        if (exebuffer == NULL) {
            return "GSF ROM memory allocation error.";
        }
        memset(exebuffer, 0, exebuffersize);
    }

    if (!psf.ReadExe(exebuffer + gsfRomStart, gsfRomSize, 0x0c))
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

const char *GSFLoader::load_psf_libs(PSFFile &psf, RawFile *file, unsigned char *&exebuffer,
                                     size_t &exebuffersize) {
    char libTagName[16];
    int libIndex = 1;
    while (true) {
        if (libIndex == 1)
            strcpy(libTagName, "_lib");
        else
            sprintf(libTagName, "_lib%d", libIndex);

        map<string, string>::iterator itLibTag = psf.tags.find(libTagName);
        if (itLibTag == psf.tags.end())
            break;

        char *fullPath;
        fullPath = GetFileWithBase(file->path().c_str(), itLibTag->second.c_str());

        // TODO: Make sure to limit recursion to avoid crashing.
        DiskFile *newRawFile = new DiskFile(fullPath);
        const char *psflibError = NULL;
        psflibError = psf_read_exe(newRawFile, exebuffer, exebuffersize);
        delete fullPath;
        delete newRawFile;

        if (psflibError != NULL)
            return psflibError;

        libIndex++;
    }
    return NULL;
}

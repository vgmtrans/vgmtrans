/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <fstream>

#include "VGMColl.h"

#include "PS1Format.h"

#include "loaders/FileLoader.h"
#include "loaders/LoaderManager.h"

using namespace std;

VGMRoot *pRoot;

VGMRoot::~VGMRoot(void) {
    DeleteVect<VGMLoader>(vLoader);
    DeleteVect<VGMFile>(vVGMFile);
}

/* FIXME: We want automatic registration */
bool VGMRoot::Init(void) {
    UI_SetRootPtr(&pRoot);

    AddScanner("NDS");
    AddScanner("Akao");
    AddScanner("FFT");
    AddScanner("HOSA");
    AddScanner("PS1");
    AddScanner("SquarePS2");
    AddScanner("SonyPS2");
    AddScanner("TriAcePS1");
    AddScanner("MP2k");
    AddScanner("HeartBeatPS1");
    AddScanner("TamSoftPS1");
    AddScanner("KonamiPS1");
    // AddScanner("Org");
    // AddScanner("QSound");
    // AddScanner("SegSat");
    // AddScanner("TaitoF3");

    // the following scanners use USE_EXTENSION
    // AddScanner("NinSnes");
    // AddScanner("SuzukiSnes");
    // AddScanner("CapcomSnes");
    // AddScanner("RareSnes");

    return true;
}

void VGMRoot::AddScanner(const string &formatname) {
    Format *fmt = Format::GetFormatFromName(formatname);
    if (!fmt)
        return;
    VGMScanner &scanner = fmt->GetScanner();
    if (!scanner.Init())
        return;
    vScanner.push_back(&scanner);
}

void VGMRoot::Exit(void) {
    UI_PreExit();
    Reset();
    UI_Exit();
}

void VGMRoot::Reset(void) {}

// opens up a file from the filesystem and scans it for known formats
bool VGMRoot::OpenRawFile(const string &filename) {
    auto newfile = std::make_shared<DiskFile>(filename);

    // if the file was set up properly, apply loaders, scan it, and add it to our list if it
    // contains vgmfiles
    return SetupNewRawFile(newfile);
}

// creates a virtual file, a RawFile that was data was created manually,
// not actually opened from the filesystem.  Used, for example, when decompressing
// the contents of PSF2 files
bool VGMRoot::CreateVirtFile(uint8_t *databuf, uint32_t fileSize, const string &filename,
                             const string &parRawFileFullPath, const VGMTag tag) {
    assert(fileSize != 0);

    auto newVirtFile =
        std::make_shared<VirtFile>(databuf, fileSize, filename, parRawFileFullPath, tag);

    return SetupNewRawFile(newVirtFile);
}

bool VGMRoot::SetupNewRawFile(std::shared_ptr<RawFile> newRawFile) {
    if (newRawFile->useLoaders()) {
        for (auto l : LoaderManager::get().loaders()) {
            l->apply(newRawFile.get());
            auto res = l->results();

            /* If the loader extracted anything we shoulnd't have to scan */
            if (!res.empty()) {
                newRawFile->setUseScanners(false);

                for (auto file : res) {
                    SetupNewRawFile(file);
                }
            }
        }
    }

    if (newRawFile->useScanners()) {
        /*
         * Make use of the extension to run only a subset of scanners.
         * Unsure how good of an idea this is
         */
        auto specific_scanners =
            ExtensionDiscriminator::instance().GetScannerList(newRawFile->extension());
        if (specific_scanners) {
            for (auto scanner : *specific_scanners) {
                scanner->Scan(newRawFile.get());
            }
        } else {
            for (auto scanner : vScanner) {
                scanner->Scan(newRawFile.get());
            }
        }
    }

    if (newRawFile->containedVGMFiles.empty()) {
        return true;
    }

    // FIXME
    auto &newref = m_activefiles.emplace_back(newRawFile);
    vRawFile.emplace_back(newref.get());
    UI_AddRawFile(newref.get());

    return true;
}

// Name says it all.
bool VGMRoot::CloseRawFile(RawFile *targFile) {
    if (!targFile) {
        return false;
    }

    auto file = find(vRawFile.begin(), vRawFile.end(), targFile);
    if (file != vRawFile.end()) {
        auto &vgmfiles = (*file)->containedVGMFiles;
        auto size = vgmfiles.size();
        for (size_t i = 0; i < size; i++) {
            pRoot->RemoveVGMFile(vgmfiles.front(), false);
            vgmfiles.erase(vgmfiles.begin());
        }

        vRawFile.erase(file);
    } else {
        L_WARN("Requested deletion for RawFile but it was not found");
    }

    return true;
}

// Adds a a VGMFile to the interface.  The UI_AddVGMFile function will handle the
// interface-specific stuff
void VGMRoot::AddVGMFile(VGMFile *theFile) {
    theFile->GetRawFile()->AddContainedVGMFile(theFile);
    vVGMFile.push_back(theFile);
    UI_AddVGMFile(theFile);
}

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::RemoveVGMFile(VGMFile *targFile, bool bRemoveFromRaw) {
    // First we should call the format's onClose handler in case it needs to use
    // the RawFile before we close it (FilenameMatcher, for ex)
    Format *fmt = targFile->GetFormat();
    if (fmt)
        fmt->OnCloseFile(targFile);

    vector<VGMFile *>::iterator iter = find(vVGMFile.begin(), vVGMFile.end(), targFile);
    if (iter != vVGMFile.end())
        vVGMFile.erase(iter);
    else
        L_WARN("Requested deletion for VGMFile but it was not found");

    if (bRemoveFromRaw)
        targFile->rawfile->RemoveContainedVGMFile(targFile);
    while (targFile->assocColls.size())
        RemoveVGMColl(targFile->assocColls.back());

    UI_RemoveVGMFile(targFile);
    delete targFile;
}

void VGMRoot::AddVGMColl(VGMColl *theColl) {
    vVGMColl.push_back(theColl);
    UI_AddVGMColl(theColl);
}

void VGMRoot::RemoveVGMColl(VGMColl *targColl) {
    targColl->RemoveFileAssocs();
    vector<VGMColl *>::iterator iter = find(vVGMColl.begin(), vVGMColl.end(), targColl);
    if (iter != vVGMColl.end())
        vVGMColl.erase(iter);
    else
        L_WARN("Requested deletion for VGMColl but it was not found");

    UI_RemoveVGMColl(targColl);
    delete targColl;
}

// This virtual function is called whenever a VGMFile is added to the interface.
// By default, it simply sorts out what type of file was added and then calls a more
// specific virtual function for the file type.  It is virtual in case a user-interface
// wants do something universally whenever any type of VGMFiles is added.
void VGMRoot::UI_AddVGMFile(VGMFile *theFile) {
    switch (theFile->GetFileType()) {
        case FILETYPE_SEQ:
            UI_AddVGMSeq((VGMSeq *)theFile);
            break;
        case FILETYPE_INSTRSET:
            UI_AddVGMInstrSet((VGMInstrSet *)theFile);
            break;
        case FILETYPE_SAMPCOLL:
            UI_AddVGMSampColl((VGMSampColl *)theFile);
            break;
        case FILETYPE_MISC:
            UI_AddVGMMisc((VGMMiscFile *)theFile);
            break;
    }
}

// Given a pointer to a buffer of data, size, and a filename, this function writes the data
// into a file on the filesystem.
bool VGMRoot::UI_WriteBufferToFile(const string &filepath, uint8_t *buf, uint32_t size) {
    ofstream outfile(filepath, ios::out | ios::trunc | ios::binary);

    if (!outfile.is_open())  // if attempt to open file failed
        return false;

    outfile.write((const char *)buf, size);
    outfile.close();
    return true;
}

bool VGMRoot::SaveAllAsRaw() {
    string dirpath = UI_GetSaveDirPath();
    if (dirpath.length() != 0) {
        for (uint32_t i = 0; i < vVGMFile.size(); i++) {
            bool result;
            VGMFile *file = vVGMFile[i];
            string filepath = dirpath + "/" + file->GetName()->c_str();
            uint8_t *buf = new uint8_t[file->unLength];  // create a buffer the size of the file
            file->GetBytes(file->dwOffset, file->unLength, buf);
            result = UI_WriteBufferToFile(filepath.c_str(), buf, file->unLength);
            delete[] buf;
        }
        return true;
    }
    return false;
}

/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <fstream>

#include "Root.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "components/seq/VGMSeq.h"
#include "components/instr/VGMInstrSet.h"
#include "components/VGMSampColl.h"
#include "components/VGMMiscFile.h"
#include "Format.h"
#include "Scanner.h"

#include "components/FileLoader.h"
#include "loaders/LoaderManager.h"
#include "components/ScannerManager.h"

VGMRoot *pRoot;

bool VGMRoot::Init() {
    UI_SetRootPtr(&pRoot);
    return true;
}

/* Opens up a file from the filesystem and scans it */
bool VGMRoot::OpenRawFile(const std::string &filename) {
    auto newfile = std::make_shared<DiskFile>(filename);
    return SetupNewRawFile(newfile);
}

/* Creates a new file backed by RAM */
bool VGMRoot::CreateVirtFile(uint8_t *databuf, uint32_t fileSize, const std::string &filename,
                             const std::string &parRawFileFullPath, VGMTag tag) {
    assert(fileSize != 0);

    auto newVirtFile =
        std::make_shared<VirtFile>(databuf, fileSize, filename, parRawFileFullPath, tag);

    return SetupNewRawFile(newVirtFile);
}

/* Applies loaders and scanners to a file, registering it if it contains anything */
bool VGMRoot::SetupNewRawFile(std::shared_ptr<RawFile> newRawFile) {
    if (newRawFile->useLoaders()) {
        for (const auto &l : LoaderManager::get().loaders()) {
            l->apply(newRawFile.get());
            auto res = l->results();

            /* If the loader extracted anything we shouldn't have to scan */
            if (!res.empty()) {
                newRawFile->setUseScanners(false);

                for (const auto &file : res) {
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
            ScannerManager::get().scanners_with_extension(newRawFile->extension());
        if (!specific_scanners.empty()) {
            for (const auto &scanner : specific_scanners) {
                scanner->Scan(newRawFile.get());
            }
        } else {
            for (const auto &scanner : ScannerManager::get().scanners()) {
                scanner->Scan(newRawFile.get());
            }
        }
    }

    if (!newRawFile->containedVGMFiles().empty()) {
        // FIXME
        auto &newref = m_activefiles.emplace_back(newRawFile);
        vRawFile.emplace_back(newref.get());
        UI_AddRawFile(newref.get());
    }

    return true;
}

bool VGMRoot::CloseRawFile(RawFile *targFile) {
    if (!targFile) {
        return false;
    }

    auto file = std::find(vRawFile.begin(), vRawFile.end(), targFile);
    if (file != vRawFile.end()) {
        auto &vgmfiles = (*file)->containedVGMFiles();
        for(auto &file : vgmfiles) {
            RemoveVGMFile(*file);
        }
        vRawFile.erase(file);
    } else {
        L_WARN("Requested deletion for RawFile but it was not found");
        return false;
    }

    return true;
}

void VGMRoot::AddVGMFile(
    std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
    vVGMFile.push_back(file);
    UI_AddVGMFile(file);
}

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::RemoveVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file, bool bRemoveFromRaw) {
    auto targFile = std::visit([](auto file) ->VGMFile * { return file; }, file);
    // First we should call the format's onClose handler in case it needs to use
    // the RawFile before we close it (FilenameMatcher, for ex)
    Format *fmt = targFile->GetFormat();
    if (fmt) {
        fmt->OnCloseFile(file);
    }

    auto iter = std::find(vVGMFile.begin(), vVGMFile.end(), file);

    if (iter != vVGMFile.end()) {
        UI_RemoveVGMFile(targFile);
        vVGMFile.erase(iter);
    } else {
        L_WARN("Requested deletion for VGMFile but it was not found");
    }

    while (!targFile->assocColls.empty()) {
        RemoveVGMColl(targFile->assocColls.back());
    }

    if (bRemoveFromRaw) 
        targFile->GetRawFile()->RemoveContainedVGMFile(file);
}

void VGMRoot::AddVGMColl(VGMColl *theColl) {
    vVGMColl.push_back(theColl);
    UI_AddVGMColl(theColl);
}

void VGMRoot::RemoveVGMColl(VGMColl *targColl) {
    auto iter = find(vVGMColl.begin(), vVGMColl.end(), targColl);
    if (iter != vVGMColl.end())
        vVGMColl.erase(iter);
    else
        L_WARN("Requested deletion for VGMColl but it was not found");

    targColl->RemoveFileAssocs();
    UI_RemoveVGMColl(targColl);
    delete targColl;
}

// This virtual function is called whenever a VGMFile is added to the interface.
// By default, it simply sorts out what type of file was added and then calls a more
// specific virtual function for the file type.  It is virtual in case a user-interface
// wants do something universally whenever any type of VGMFiles is added.
void VGMRoot::UI_AddVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
    if(auto seq = std::get_if<VGMSeq *>(&file)) {
        UI_AddVGMSeq(*seq);
    } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
        UI_AddVGMInstrSet(*instr);
    } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
        UI_AddVGMSampColl(*sampcoll);
    } else if(auto misc = std::get_if<VGMMiscFile *>(&file)) {
        UI_AddVGMMisc(*misc);
    }
}

// Given a pointer to a buffer of data, size, and a filename, this function writes the data
// into a file on the filesystem.
bool VGMRoot::UI_WriteBufferToFile(const std::string &filepath, uint8_t *buf, uint32_t size) {
    std::ofstream outfile(filepath, std::ios::out | std::ios::trunc | std::ios::binary);

    if (!outfile.is_open()) {
        return false;
    }

    outfile.write(reinterpret_cast<char *>(buf), size);
    outfile.close();
    return true;
}

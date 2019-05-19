/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PS1SeqScanner.h"
#include "PS1Format.h"
#include "PSXSPU.h"

#define SRCH_BUF_SIZE 0x20000

PS1SeqScanner::PS1SeqScanner(void) {}

PS1SeqScanner::~PS1SeqScanner(void) {}

void PS1SeqScanner::Scan(RawFile *file, void *info) {
    SearchForPS1Seq(file);
    std::vector<Vab *> vabs = SearchForVab(file);
    if (vabs.size() == 0 || vabs[0]->GetStartOffset() != 0) {
        PSXSampColl::SearchForPSXADPCM(file, PS1Format::name);
    }
    return;
}

std::vector<PS1Seq *> PS1SeqScanner::SearchForPS1Seq(RawFile *file) {
    uint32_t nFileLength = file->size();
    std::vector<PS1Seq *> loadedFiles;
    for (uint32_t i = 0; i + 4 < nFileLength; i++) {
        if ((*file)[i] == 'p' && (*file)[i + 1] == 'Q' && (*file)[i + 2] == 'E' &&
            (*file)[i + 3] == 'S') {
            PS1Seq *newPS1Seq = new PS1Seq(file, i);
            if (newPS1Seq->LoadVGMFile()) {
                loadedFiles.push_back(newPS1Seq);
            } else {
                delete newPS1Seq;
            }
        }
    }
    return loadedFiles;
}

std::vector<Vab *> PS1SeqScanner::SearchForVab(RawFile *file) {
    uint32_t nFileLength = file->size();
    std::vector<Vab *> loadedFiles;
    for (uint32_t i = 0; i + 4 < nFileLength; i++) {
        if ((*file)[i] == 'p' && (*file)[i + 1] == 'B' && (*file)[i + 2] == 'A' &&
            (*file)[i + 3] == 'V') {
            Vab *newVab = new Vab(file, i);
            if (newVab->LoadVGMFile()) {
                loadedFiles.push_back(newVab);
            } else {
                delete newVab;
            }
        }
    }
    return loadedFiles;
}

/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PS1SeqScanner.h"

#include <algorithm>
#include <functional>
#include "PS1Format.h"
#include "PSXSPU.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<PS1SeqScanner> s_ps1seq("PS1SEQ");
}

constexpr int SRCH_BUF_SIZE = 0x20000;

PS1SeqScanner::PS1SeqScanner(void) {}

PS1SeqScanner::~PS1SeqScanner(void) {}

void PS1SeqScanner::Scan(RawFile *file, void *info) {
    auto seqs = SearchForPS1Seq(file);
    auto vabs = SearchForVab(file);
    if (vabs.empty() || vabs[0]->dwOffset != 0) {
        PSXSampColl::SearchForPSXADPCM(file, PS1Format::name);
    }
}

std::vector<PS1Seq *> PS1SeqScanner::SearchForPS1Seq(RawFile *file) {
    uint32_t nFileLength = file->size();
    std::vector<PS1Seq *> loadedFiles;

    using namespace std::string_literals;
    const std::string signature = "SEQp"s;
    auto it = std::search(file->begin(), file->end(),
                          std::boyer_moore_searcher(signature.rbegin(), signature.rend()));

    while (it != file->end()) {
        PS1Seq *newPS1Seq = new PS1Seq(file, it - file->begin());
        if (newPS1Seq->LoadVGMFile()) {
            loadedFiles.push_back(newPS1Seq);
        } else {
            delete newPS1Seq;
        }

        it = std::search(std::next(it), file->end(),
                         std::boyer_moore_searcher(signature.rbegin(), signature.rend()));
    }

    return loadedFiles;
}

std::vector<Vab *> PS1SeqScanner::SearchForVab(RawFile *file) {
    uint32_t nFileLength = file->size();
    std::vector<Vab *> loadedFiles;

    using namespace std::string_literals;
    const std::string signature = "VABp"s;
    auto it = std::search(file->begin(), file->end(),
                          std::boyer_moore_searcher(signature.rbegin(), signature.rend()));

    while (it != file->end()) {
        Vab *newVab = new Vab(file, it - file->begin());
        if (newVab->LoadVGMFile()) {
            loadedFiles.push_back(newVab);
        } else {
            delete newVab;
        }

        it = std::search(std::next(it), file->end(),
                         std::boyer_moore_searcher(signature.rbegin(), signature.rend()));
    }

    return loadedFiles;
}

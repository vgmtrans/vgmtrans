#pragma once

#include "components/VGMColl.h"
#include "components/seq/VGMSeq.h"
#include "conversion/DLSFile.h"
#include "conversion/SF2File.h"

namespace conversion {

// fixme: better solution for nttp
namespace ConversionTarget {
    inline unsigned constexpr MIDI = 1u << 0u;
    inline unsigned constexpr DLS = 1u << 1u;
    inline unsigned constexpr SF2 = 1u << 2u;
}

template <unsigned options>
void SaveAs(VGMColl &coll, const std::string &dir_path) {
    if constexpr ((options & ConversionTarget::MIDI) != 0) {
        auto filepath = dir_path + "/" + ConvertToSafeFileName(coll.GetName()) + ".mid";
        coll.seq->SaveAsMidi(filepath);
    }

    if constexpr ((options & ConversionTarget::DLS) != 0) {
        DLSFile dlsfile;
        if (coll.CreateDLSFile(dlsfile)) {
            auto filepath = dir_path + "/" + ConvertToSafeFileName(coll.GetName()) + ".dls";
            dlsfile.SaveDLSFile(filepath);
        }
    }

    if constexpr ((options & ConversionTarget::SF2) != 0) {
        SF2File *sf2file = coll.CreateSF2File();
        if (sf2file) {
            auto filepath = dir_path + "/" + ConvertToSafeFileName(coll.GetName()) + ".sf2";
            sf2file->SaveSF2File(filepath);
        }
    }
}

}  // namespace conversion
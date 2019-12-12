/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "QSoundSeq.h"
#include "QSoundInstr.h"
#include "MAMELoader.h"
#include <fmt/format.h>

using namespace std;

//#define PROG_INFO_TABLE_OFFSET 0x7000
//#define qs_samp_info_TABLE_OFFSET 0x8000

void QSoundScanner::Scan(RawFile *file, void *info) {
    MAMEGameEntry *gameentry = (MAMEGameEntry *)info;
    MAMERomGroupEntry *seqRomGroupEntry = gameentry->GetRomGroupOfType("audiocpu");
    MAMERomGroupEntry *sampsRomGroupEntry = gameentry->GetRomGroupOfType("qsound");
    if (!seqRomGroupEntry || !sampsRomGroupEntry)
        return;
    uint32_t seq_table_offset;
    uint32_t seq_table_length = 0;
    uint32_t instr_table_offset;
    uint32_t samp_table_offset;
    uint32_t samp_table_length = 0;
    uint32_t artic_table_offset = 0;
    uint32_t artic_table_length = 0x800;
    uint32_t num_instr_banks;
    uint32_t instr_tables_end = 0;
    if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
        !seqRomGroupEntry->GetHexAttribute("seq_table", &seq_table_offset) ||
        !seqRomGroupEntry->GetHexAttribute("samp_table", &samp_table_offset) ||
        !seqRomGroupEntry->GetAttribute("num_instr_banks", &num_instr_banks))
        return;
    seqRomGroupEntry->GetHexAttribute("samp_table_length", &samp_table_length);

    QSoundVer ver = GetVersionEnum(gameentry->fmt_version_str);
    if (ver == VER_UNDEFINED) {
        L_ERROR("Unknown QSound version '{}'", gameentry->fmt_version_str);

        return;
    }

    if (ver < VER_116B) {
        if (!seqRomGroupEntry->GetHexAttribute("instr_table", &instr_table_offset))
            return;
    } else if (!seqRomGroupEntry->GetHexAttribute("instr_table_ptrs", &instr_table_offset))
        return;
    if (ver >= VER_130 && !seqRomGroupEntry->GetHexAttribute("artic_table", &artic_table_offset))
        return;

    QSoundInstrSet *instrset = 0;
    QSoundSampColl *sampcoll = 0;
    QSoundSampleInfoTable *sampInfoTable = 0;
    QSoundArticTable *articTable = 0;

    string artic_table_name = fmt::format("{} articulation table", gameentry->name.c_str());
    string instrset_name = fmt::format("{} instrument set", gameentry->name.c_str());
    string sampcoll_name = fmt::format("{} sample collection", gameentry->name.c_str());
    string samp_info_table_name = fmt::format("{} sample info table", gameentry->name.c_str());
    string seq_table_name = fmt::format("{} sequence pointer table", gameentry->name.c_str());

    RawFile *programFile = seqRomGroupEntry->file;
    RawFile *samplesFile = sampsRomGroupEntry->file;
    uint32_t nProgramFileLength = programFile->size();

    // LOAD INSTRUMENTS AND SAMPLES

    // fix because Vampire Savior sample table bleeds into artic table and no way to detect this
    if (!samp_table_length && (artic_table_offset > samp_table_offset))
        samp_table_length = artic_table_offset - samp_table_offset;
    sampInfoTable = new QSoundSampleInfoTable(programFile, samp_info_table_name, samp_table_offset,
                                              samp_table_length);
    sampInfoTable->LoadVGMFile();
    if (artic_table_offset) {
        articTable = new QSoundArticTable(programFile, artic_table_name, artic_table_offset,
                                          artic_table_length);
        if (!articTable->LoadVGMFile()) {
            delete articTable;
            articTable = NULL;
        }
    }

    instrset = new QSoundInstrSet(programFile, ver, instr_table_offset, num_instr_banks,
                                  sampInfoTable, articTable, instrset_name);
    if (!instrset->LoadVGMFile()) {
        delete instrset;
        instrset = NULL;
    }
    sampcoll = new QSoundSampColl(samplesFile, instrset, sampInfoTable, 0, 0, sampcoll_name);
    if (!sampcoll->LoadVGMFile()) {
        delete sampcoll;
        sampcoll = NULL;
    }

    // LOAD SEQUENCES FROM SEQUENCE TABLE AND CREATE COLLECTIONS
    //  Set the seq table length to be the distance to the first seq pointer
    //  as sequences seem to immediately follow the seq table.
    unsigned int k = 0;
    uint32_t seqPointer = 0;
    while (seqPointer == 0)
        seqPointer = programFile->GetWordBE(seq_table_offset + (k++ * 4)) & 0x0FFFFF;
    seq_table_length = seqPointer - seq_table_offset;

    // Add SeqTable as Miscfile
    VGMMiscFile *seqTable = new VGMMiscFile(QSoundFormat::name, seqRomGroupEntry->file,
                                            seq_table_offset, seq_table_length, seq_table_name);
    if (!seqTable->LoadVGMFile()) {
        delete seqTable;
        seqTable = NULL;
    }

    for (k = 0; (seq_table_length == 0 || k < seq_table_length); k += 4) {
        // & 0x0FFFFF because SSF2 sets 0x100000 for some reason
        seqPointer = programFile->GetWordBE(seq_table_offset + k) & 0x0FFFFF;
        if (seqPointer == 0)
            continue;
        seqTable->AddSimpleItem(seq_table_offset + k, 4, "Sequence Pointer");

        VGMColl *coll = new VGMColl(fmt::format("{} song {}", gameentry->name.c_str(), k / 4));

        QSoundSeq *newSeq = new QSoundSeq(programFile, seqPointer, ver,
                                          fmt::format("{} seq {}", gameentry->name.c_str(), k / 4));
        if (newSeq->LoadVGMFile()) {
            coll->UseSeq(newSeq);
            coll->AddInstrSet(instrset);
            coll->AddSampColl(sampcoll);
            coll->AddMiscFile(sampInfoTable);
            if (articTable)
                coll->AddMiscFile(articTable);
            if (!coll->Load()) {
                delete coll;
            }
        } else {
            delete newSeq;
            delete coll;
        }
    }

    return;
}

QSoundVer QSoundScanner::GetVersionEnum(const std::string &versionStr) {
    if (versionStr == "1.00")
        return VER_100;
    if (versionStr == "1.01")
        return VER_101;
    if (versionStr == "1.03")
        return VER_103;
    if (versionStr == "1.04")
        return VER_104;
    if (versionStr == "1.05a")
        return VER_105A;
    if (versionStr == "1.05c")
        return VER_105C;
    if (versionStr == "1.05")
        return VER_105;
    if (versionStr == "1.06b")
        return VER_106B;
    if (versionStr == "1.15c")
        return VER_115C;
    if (versionStr == "1.15")
        return VER_115;
    if (versionStr == "2.01b")
        return VER_201B;
    if (versionStr == "1.16b")
        return VER_116B;
    if (versionStr == "1.16")
        return VER_116;
    if (versionStr == "1.30")
        return VER_130;
    if (versionStr == "1.31")
        return VER_131;
    if (versionStr == "1.40")
        return VER_140;
    if (versionStr == "1.71")
        return VER_171;
    if (versionStr == "1.80")
        return VER_180;
    return VER_UNDEFINED;
}

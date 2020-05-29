/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SPCLoader.h"

void SPCLoader::apply(const RawFile *file) {
    if (file->size() < 0x10180) {
        return;
    }

    if (!std::equal(file->begin(), file->begin() + 27, "SNES-SPC700 Sound File Data") ||
            file->GetShort(0x21) != 0x1a1a) {
        return;
    }

    auto spcFile = std::make_shared<VirtFile>(*file, 0x100, 0x10000);

    std::vector<uint8_t> dsp(file->data() + 0x10100, file->data() + 0x10100 + 0x80);
    spcFile->tag.binaries["dsp"] = dsp;

    // Parse [ID666](http://vspcplay.raphnet.net/spc_file_format.txt) if available.
    if (file->GetByte(0x23) == 0x1a) {
        char s[256];

        file->GetBytes(0x2e, 32, s);
        s[32] = '\0';
        std::string s_str = s;
        spcFile->tag.title = (s_str);

        file->GetBytes(0x4e, 32, s);
        s[32] = '\0';
        s_str = s;
        spcFile->tag.album = (s_str);

        file->GetBytes(0x7e, 32, s);
        s[32] = '\0';
        s_str = s;
        spcFile->tag.comment = (s_str);

        if (file->GetByte(0xd2) < 0x30) {
            // binary format
            file->GetBytes(0xb0, 32, s);
            s[32] = '\0';
            s_str = s;
            spcFile->tag.artist = (s_str);

            spcFile->tag.length = (double)(file->GetWord(0xa9) & 0xffffff);
        } else {
            // text format
            file->GetBytes(0xb1, 32, s);
            s[32] = '\0';
            s_str = s;
            spcFile->tag.artist = (s_str);

            file->GetBytes(0xa9, 3, s);
            s[3] = '\0';
            spcFile->tag.length = strtoul(s, nullptr, 10);
        }
    }

    // Parse Extended ID666 if available
    if (file->size() >= 0x10208) {
        uint32_t xid6_end_offset = 0x10208 + file->GetWord(0x10204);
        if (std::equal(file->begin() + 0x10200, file->begin() + 0x10204, "xid6") && file->size() >= xid6_end_offset) {
            uint32_t xid6_offset = 0x10208;
            while (xid6_offset + 4 < xid6_end_offset) {
                uint8_t xid6_id = file->GetByte(xid6_offset);
                uint8_t xid6_type = file->GetByte(xid6_offset + 1);
                uint16_t xid6_data = file->GetByte(xid6_offset + 2);

                // get the length of this field
                uint16_t xid6_length;
                if (xid6_type == 0) {
                    xid6_length = 0;
                } else {
                    xid6_length = xid6_data;
                }

                // check size error
                if (xid6_end_offset < xid6_offset + 4 + xid6_length) {
                    break;
                }

                switch (xid6_type) {
                    case 0:
                        // Data
                        break;

                    case 1: {
                        // String (data contains null character)
                        std::string s_str =
                            std::string((char *)(file->data() + xid6_offset + 4), xid6_length - 1);
                        std::string xid6_string = (s_str);
                        switch (xid6_id) {
                            case 1:
                                // Song name
                                spcFile->tag.title = xid6_string;
                                break;

                            case 2:
                                // Game name
                                spcFile->tag.album = xid6_string;
                                break;

                            case 3:
                                // Artist
                                spcFile->tag.artist = xid6_string;
                                break;

                            case 7:
                                // Comments
                                spcFile->tag.comment = xid6_string;
                                break;
                        }
                        break;
                    }

                    case 4:
                        // Integer
                        break;

                    default:
                        // Unknown
                        break;
                }

                xid6_offset += 4 + ((xid6_length + 3) / 4 * 4);
            }
        }
    }

    enqueue(spcFile);
}

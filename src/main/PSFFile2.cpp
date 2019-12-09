/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSFFile2.h"

#include <exception>
#include <algorithm>
#include <zlib.h>

PSFFile2::PSFFile2(RawFile *file) {
    uint32_t fileSize = file->size();
    if (fileSize < 0x10) {
        throw std::length_error("PSF file smaller than header, likely corrupt");
    }

    uint8_t signature[4];
    file->GetBytes(0, 4, signature);
    if (memcmp(signature, "PSF", 3) != 0) {
        throw std::runtime_error("Invalid PSF signature");
    }

    m_version = signature[3];

    uint32_t reservedarea_size = file->GetWord(4);
    uint32_t exe_size = file->GetWord(8);
    m_exe_CRC = file->GetWord(12);

    if ((reservedarea_size > fileSize) || (exe_size > fileSize) ||
        ((16 + reservedarea_size + exe_size) > fileSize)) {
        throw std::runtime_error("PSF header is corrupt or invalid");
    }

    /* FIXME: Use std::copy whenever RawFile gets iterators */
    std::vector<char> tmp_data;
    tmp_data.resize(exe_size);
    file->GetBytes(16 + reservedarea_size, exe_size, tmp_data.data());

    if (m_exe_CRC != crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(tmp_data.data()),
                           tmp_data.size())) {
        throw std::runtime_error("CRC32 mismatch, data is corrupt");
    }

    m_reserved_data.resize(reservedarea_size);
    file->GetBytes(16, reservedarea_size, m_reserved_data.data());

    m_exe_data = zdecompress(tmp_data);

    // Check existence of tag section.
    uint32_t tag_section_offset = 16 + reservedarea_size + exe_size;
    uint32_t tag_section_size = fileSize - tag_section_offset;
    bool valid_tag_section = false;
    if (tag_section_size >= PSF_TAG_SIG_LEN) {
        uint8_t tagSig[5];
        file->GetBytes(tag_section_offset, PSF_TAG_SIG_LEN, tagSig);
        if (memcmp(tagSig, PSF_TAG_SIG, PSF_TAG_SIG_LEN) == 0) {
            valid_tag_section = true;
        }
    }

    if (valid_tag_section) {
        std::vector<char> tag_section;
        tag_section.resize(tag_section_size);
        file->GetBytes(tag_section_offset, tag_section_size, tag_section.data());
        tag_section.push_back('\0');
        parseTags(tag_section);
    }
}

void PSFFile2::parseTags(std::vector<char> &tag_section) {
    size_t needle = PSF_TAG_SIG_LEN;
    while (needle < tag_section.size()) {
        // Search the end position of the current line.
        auto line_end = std::find(std::begin(tag_section) + needle, std::end(tag_section), 0x0A);
        if (line_end == std::end(tag_section)) {
            // Tag section must end with a newline.
            // Read the all remaining bytes if a newline lacks though.
            line_end = std::begin(tag_section) + tag_section.size() - 1;
        }

        // Replace the newline with NUL,
        // for better C string function compatibility.
        *line_end = '\0';

        // Search the variable=value separator.
        auto separator_token =
            std::find(std::begin(tag_section) + needle, std::end(tag_section), '=');
        if (separator_token == std::end(tag_section)) {
            // Blank lines, or lines not of the form "variable=value", are ignored.
            needle = line_end + 1 - std::begin(tag_section);
            continue;
        }

        // Determine the start/end position of variable.
        auto name_token = std::begin(tag_section) + needle;
        auto name_token_end = separator_token;
        auto value_token = separator_token + 1;
        auto value_token_end = line_end;

        // Whitespace at the beginning/end of the line and before/after the = are ignored.
        // All characters 0x01-0x20 are considered whitespace.
        // (There must be no null (0x00) characters.)
        while (name_token_end > name_token && *(name_token_end - 1) <= 0x20)
            name_token_end--;
        while (value_token_end > value_token && *(value_token_end - 1) <= 0x20)
            value_token_end--;
        while (name_token < name_token_end && *(name_token) <= 0x20)
            name_token++;
        while (value_token < value_token_end && *(value_token) <= 0x20)
            value_token++;

        // Read variable=value as string.
        std::string name(tag_section.data() + std::distance(std::begin(tag_section), name_token),
                         name_token_end - name_token);
        std::string value(tag_section.data() + std::distance(std::begin(tag_section), value_token),
                          value_token_end - value_token);

        // Multiple-line variables must appear as consecutive lines using the same variable name.
        // For instance:
        //   comment=This is a
        //   comment=multiple-line
        //   comment=comment.
        // Therefore, check if the variable had already appeared.
        auto it = m_tags.lower_bound(name);
        if (it != m_tags.end() && it->first == name) {
            it->second += "\n";
            it->second += value;
        } else {
            m_tags.insert(it, make_pair(name, value));
        }

        needle = line_end + 1 - std::begin(tag_section);
    }
}

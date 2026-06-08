/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "VGMTag.h"

#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

class RawFile;
class PSFFile;

namespace vgmtrans::psf {

inline constexpr u8 PSF1_VERSION = 0x01;
inline constexpr u8 SSF_VERSION = 0x11;
inline constexpr u8 GSF_VERSION = 0x22;
inline constexpr u8 SNSF_VERSION = 0x23;
inline constexpr u8 NDS2SF_VERSION = 0x24;
inline constexpr u8 NCSF_VERSION = 0x25;

inline constexpr size_t PSF1_DATA_OFFSET = 0x800;
inline constexpr size_t SSF_DATA_OFFSET = 0x04;
inline constexpr size_t GSF_DATA_OFFSET = 0x0C;
inline constexpr size_t SNSF_DATA_OFFSET = 0x08;
inline constexpr size_t NDS2SF_DATA_OFFSET = 0x08;
inline constexpr size_t NCSF_DATA_OFFSET = 0x00;

[[nodiscard]] inline std::optional<size_t> dataOffsetForVersion(u8 version) {
  switch (version) {
    case PSF1_VERSION:
      return PSF1_DATA_OFFSET;
    case SSF_VERSION:
      return SSF_DATA_OFFSET;
    case GSF_VERSION:
      return GSF_DATA_OFFSET;
    case SNSF_VERSION:
      return SNSF_DATA_OFFSET;
    case NDS2SF_VERSION:
      return NDS2SF_DATA_OFFSET;
    case NCSF_VERSION:
      return NCSF_DATA_OFFSET;
    default:
      return std::nullopt;
  }
}

}  // namespace vgmtrans::psf

class PSFFile {
   public:
    explicit PSFFile(const RawFile &file);
    ~PSFFile() = default;

    static VGMTag tagFromPSFFile(const PSFFile& psf);

    [[nodiscard]] u8 version() const noexcept { return m_version; }
    [[nodiscard]] const std::map<std::string, std::string> &tags() const noexcept { return m_tags; }
    [[nodiscard]] const std::vector<unsigned char> &exe() const noexcept { return m_exe_data; }
    [[nodiscard]] const std::vector<unsigned char> &reservedSection() const noexcept { return m_reserved_data; }
    [[nodiscard]] std::optional<std::string> primaryLibName() const;

    template <typename T> T getExe(size_t ind) const {
        assert(ind + sizeof(T) <= m_exe_data.size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (m_exe_data[ind + i] << (i * CHAR_BIT));
        }

        return value;
    }

    template <typename T>
    T getRes(size_t ind) const {
        assert(ind + sizeof(T) <= m_reserved_data.size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (m_reserved_data[ind + i] << (i * CHAR_BIT));
        }

        return value;
    }

   private:
    u8 m_version;
    u32 m_exe_CRC;
    std::vector<unsigned char> m_exe_data;
    std::vector<unsigned char> m_reserved_data;
    std::map<std::string, std::string> m_tags;

    void parseTags(std::span<const char> data);
};

#include <catch.hpp>

#include <algorithm>
#include <fstream>

#include "RawFile.h"

TEST_CASE("DiskFile") {
    {
        std::ofstream out("testfile.bin");
        out << "Nice-data-sequence";
    }

    DiskFile file("testfile.bin");

    REQUIRE(file.size() == 18);
    REQUIRE(file.name() == L"testfile.bin");
    REQUIRE(file.GetExtension() == L"bin");

    REQUIRE(file.IsValidOffset(0) == true);
    REQUIRE(file.IsValidOffset(123) == false);

    /* In the future:
    REQUIRE(std::equal(file.begin(), file.end(), "Nice-data-sequence"));
    REQUIRE(std::equal(file.cbegin(), file.cend(), "Nice-data-sequence"));
    REQUIRE(std::equal(file.rbegin(), file.rend(), "ecneuqes-atad-eciN"));
    REQUIRE(std::equal(file.crbegin(), file.crend(), "ecneuqes-atad-eciN"));
    */

    REQUIRE(file[0] == 'N');
    REQUIRE(file.get<uint8_t>(0) == 0x4e);
    REQUIRE(file.get<uint16_t>(0) == 0x694e);
    REQUIRE(file.getBE<uint16_t>(0) == 0x4e69);
    REQUIRE(file.get<uint32_t>(0) == 0x6563694e);
}

TEST_CASE("MemFile from DiskFile") {
    {
        std::ofstream out("testfile2.bin");
        out << "Nice-data-sequence";
    }

    DiskFile dfile("testfile.bin");
    VirtFile file(dfile);

    REQUIRE(file.GetExtension() == L"bin");

    /* In the future:
    REQUIRE(file.size() == dfile.size());
    REQUIRE(std::equal(file.begin(), file.end(), "Nice-data-sequence"));
    REQUIRE(std::equal(file.cbegin(), file.cend(), "Nice-data-sequence"));
    REQUIRE(std::equal(file.rbegin(), file.rend(), "ecneuqes-atad-eciN"));
    REQUIRE(std::equal(file.crbegin(), file.crend(), "ecneuqes-atad-eciN"));
    */

    REQUIRE(file[0] == 'N');
    REQUIRE(file.get<uint8_t>(0) == 0x4e);
    REQUIRE(file.get<uint16_t>(0) == 0x694e);
    REQUIRE(file.getBE<uint16_t>(0) == 0x4e69);
    REQUIRE(file.get<uint32_t>(0) == 0x6563694e);
}

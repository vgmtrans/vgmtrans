#include <catch.hpp>
#include <fstream>

#include "PSFFile2.h"
#include "RawFile.h"

TEST_CASE("0-sized exe sections") {
    {
        std::ofstream f("zero.psf", std::ios::binary);
        f.write("PSF\xFF\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);
    }

    PSFFile2 psf(DiskFile{"zero.psf"});
    REQUIRE(psf.version() == 0xFF);

    REQUIRE(psf.exe().empty() == true);
    REQUIRE(psf.reservedSection().empty() == true);

    REQUIRE(psf.tags().empty() == true);   
}

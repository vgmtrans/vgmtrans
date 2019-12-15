#include <catch.hpp>
#include "loaders/LoaderManager.h"

TEST_CASE("Registered loaders") {
    REQUIRE(LoaderManager::get().loaders().empty() == false);
}

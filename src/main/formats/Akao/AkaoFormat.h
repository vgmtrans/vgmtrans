#pragma once
#include "AkaoMatcher.h"
#include "AkaoScanner.h"
#include "Format.h"
#include "VGMColl.h"

#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

class AkaoInstrSet;
struct AkaoArt;
class AkaoSampColl;

// ********
// AkaoColl
// ********

class AkaoColl final : public VGMColl {
public:
  explicit AkaoColl(std::string name = "Unnamed Collection")
      : VGMColl(std::move(name)) {}

  bool loadMain() override;
  std::tuple<std::unordered_map<u32, AkaoArt *>, std::unordered_map<u32, int>,
             std::unordered_map<u32, AkaoSampColl *>>
  mapSampleCollections() const;
};

// **********
// AkaoFormat
// **********

BEGIN_FORMAT(Akao)
  USING_SCANNER(AkaoScanner)
  USING_MATCHER(AkaoMatcher)
  USING_COLL(AkaoColl)
END_FORMAT()

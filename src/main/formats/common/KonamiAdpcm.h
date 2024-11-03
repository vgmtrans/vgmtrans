#pragma once

#include "VGMSamp.h"

class K054539AdpcmSamp
    : public VGMSamp {
public:
  K054539AdpcmSamp(
      VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t theRate, std::string name
  );
  ~K054539AdpcmSamp() override;

  double compressionRatio() override;
  void convertToStdWave(uint8_t *buf) override;
};

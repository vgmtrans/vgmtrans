#pragma once
#include "Scanner.h"
#include "HeartBeatPS1Seq.h"
#include "Vab.h"

class HeartBeatPS1Scanner:
    public VGMScanner {
 public:
  HeartBeatPS1Scanner(void);
  virtual ~HeartBeatPS1Scanner(void);

  virtual void Scan(RawFile *file, void *info = 0);
  std::vector<VGMFile *> SearchForHeartBeatPS1VGMFile(RawFile *file);
};

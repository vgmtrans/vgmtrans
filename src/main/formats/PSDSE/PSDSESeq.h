#include <string>
#include <vector>
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "PSDSEFormat.h"
#include "RawFile.h"

class PSDSESeq : public VGMSeq {
public:
  PSDSESeq(RawFile* file, uint32_t offset, uint32_t length = 0,
           std::string name = "PSDSE Sequence");
  virtual ~PSDSESeq() = default;

  virtual bool parseHeader() override;
  virtual bool parseTrackPointers() override;
  virtual void resetVars() override;

  uint16_t version;
};

class PSDSETrack : public SeqTrack {
public:
  PSDSETrack(PSDSESeq* parentSeq, long offset = 0, long length = 0);
  virtual void resetVars() override;
  virtual bool readEvent() override;

private:
  int8_t currentOctave;
  uint32_t lastNoteDuration;
  uint32_t lastWaitDuration;
  uint32_t pitchBendRangeSemitones;
};

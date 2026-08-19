#include <string>
#include <vector>
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "PSDSEFormat.h"
#include "PSDSEHeader.h"
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

private:
  Endianness m_endianness = Endianness::Little;
};

class PSDSETrack : public SeqTrack {
public:
  PSDSETrack(PSDSESeq* parentSeq, long offset, long length, uint8_t channel);
  virtual void resetVars() override;
  virtual bool readEvent() override;

protected:
  void setChannelAndGroupFromTrkNum(int trackNum) override;

private:
  uint8_t m_channel;
  int8_t currentOctave;
  uint32_t lastNoteDuration;
  uint32_t lastWaitDuration;
  uint32_t pitchBendRangeSemitones;
};

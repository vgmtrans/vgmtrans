#pragma once

#include "Types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "Format.h"			//Replace with MP2k-specific format header when that's ready


//--------------------------------------------------------------
//		HOSASeq
//--------------------------------------------------------------
class HOSASeq: public VGMSeq {
 public:
  HOSASeq(RawFile *file, u32 offset, const std::string &name = "HOSA Seq");
  ~HOSASeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  u32 id() const override { return assocHOSA_ID; }

 protected:
  u16 seqID;
  u16 assocHOSA_ID;
};


//--------------------------------------------------------------
//		HOSATrack
//--------------------------------------------------------------
class HOSATrack: public SeqTrack {
 public:
  HOSATrack(HOSASeq *parentFile, u32 offset = 0, u32 length = 0);

  bool readEvent() override;
  void ReadDeltaTime(unsigned char cCom_bit5, unsigned int *iVariable);
  u32 decodeVariable();    //Decode of 可変長

 public:
  u32 iDeltaTimeCom;     //Default delta time for Command
  u32 iDeltaTimeNote;    //Default delta time for Note
  u32 iLengthTimeNote;   //Default length for Note
//  unsigned int iDeltaTimeCounter;	//Counter of delta time for note/command.
//  vector<char> listNote;		//Key On Note
//  vector<unsigned int> listLength;		//
  s8 cVelocity;           //Default velocity
  s8 cNoteNum;            //Default Note Number
  u8 cTempo{};           //Tempo
  u8 cInstrument{};
  u8 cVolume{};
  u8 cPanpot{};
  u8 cExpression{};
};

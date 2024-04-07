#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "Format.h"			//Replace with MP2k-specific format header when that's ready


//--------------------------------------------------------------
//		HOSASeq
//--------------------------------------------------------------
class HOSASeq: public VGMSeq {
 public:
  HOSASeq(RawFile *file, uint32_t offset, const std::string &name = "HOSA Seq");
  virtual ~HOSASeq(void);

  virtual bool GetHeaderInfo(void);
  virtual bool
      GetTrackPointers(void);    //Function to find all of the track pointers.   Returns number of total tracks.
  virtual uint32_t GetID() { return assocHOSA_ID; }

 protected:
  uint16_t seqID;
  uint16_t assocHOSA_ID;
};


//--------------------------------------------------------------
//		HOSATrack
//--------------------------------------------------------------
class HOSATrack: public SeqTrack {
 public:
  HOSATrack(HOSASeq *parentFile, long offset = 0, long length = 0);

  virtual bool ReadEvent(void);
  void ReadDeltaTime(unsigned char cCom_bit5, unsigned int *iVariable);
  uint32_t DecodeVariable();    //Decode of 可変長

 public:
  uint32_t iDeltaTimeCom;     //Default delta time for Command
  uint32_t iDeltaTimeNote;    //Default delta time for Note
  uint32_t iLengthTimeNote;   //Default length for Note
//  unsigned int iDeltaTimeCounter;	//Counter of delta time for note/command.
//  vector<char> listNote;		//Key On Note
//  vector<unsigned int> listLength;		//
  int8_t cVelocity;           //Default velocity
  int8_t cNoteNum;            //Default Note Number
  uint8_t cTempo;             //Tempo
  uint8_t cInstrument;
  uint8_t cVolume;
  uint8_t cPanpot;
  uint8_t cExpression;
};

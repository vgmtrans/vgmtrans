// Code from https://github.com/bgporter/JuceRpc
// referenced in "An RPC Framework for JUCE" - https://artandlogic.com/rpc-framework-juce-2/
// Copyright 2016 Art & Logic Software Development

#pragma once

#include "juce_core/juce_core.h"

class PendingCall
{
public:
PendingCall(uint32_t sequence)
    : fSequence(sequence)
{

}

/**
  * Tell our waitable event to block here, infinitely (default), or for a specified
  * # of ms.
  * @param  milliseconds ms to wait for the call to complete; -1 == infinite wait.
  * @return              true if we were signalled, false if we timed out.
 */
bool Wait(int milliseconds=-1)
{
  return fEvent.wait(milliseconds);
}

void Signal() const { fEvent.signal(); }

uint32_t GetSequence() const { return fSequence; }

void SetMemoryBlock(const juce::MemoryBlock& mb) { fData = mb; }

const juce::MemoryBlock& GetMemoryBlock() const { return fData; }

bool operator==(const PendingCall& rhs) const { return (rhs.fSequence == fSequence);};

private:

/**
  * Sequence number of this function call.
 */
  uint32_t fSequence;

/**
  * An event for the returned message to signal.
 */
  juce::WaitableEvent fEvent;

/**
  * ...and a place to store the data returned from the server.
 */
  juce::MemoryBlock fData;



public:
/**
  * NOTE that the type and name of the linked list pointer are required
  * to be this by JUCE
 */
  juce::LinkedListPointer<PendingCall> nextListItem;
};


class PendingCallList
{
public:
  PendingCallList();

  ~PendingCallList();

  /**
    * Append a new PendingCall object to the end of our list.
    * @param call [description]
   */
  void Append(PendingCall* call);

  /**
    * Look for a pending call with the specified sequence id.
    * @param  sequence id of the call we're looking for.
    * @return          nullptr if there's no such id.
   */
  PendingCall* FindCallBySequence(uint32_t sequence);

  /**
    * Remove this object from the list, but do not delete it.
    * @param pending pointer to the object.
   */
  void Remove(PendingCall* pending, bool alsoDelete=false);


  int Size() const;


private:

  juce::LinkedListPointer<PendingCall> fPendingCalls;

  /**
    * Function calls/returns will happen on separate threads.
   */
  juce::CriticalSection fMutex;
};


class ScopedPendingCall
{
public:
  ScopedPendingCall(PendingCallList& list, PendingCall* call);
  ~ScopedPendingCall();

private:
  PendingCallList& fList;
  PendingCall* fCall;
};

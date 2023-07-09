// Code from https://github.com/bgporter/JuceRpc
// referenced in "An RPC Framework for JUCE" - https://artandlogic.com/rpc-framework-juce-2/
// Copyright 2016 Art & Logic Software Development

#include "PendingCalls.h"

using namespace juce;

PendingCallList::PendingCallList() {

}

PendingCallList::~PendingCallList() {

}

void PendingCallList::Append(PendingCall* call) {
  const ScopedLock lock(fMutex);
  fPendingCalls.append(call);
}


PendingCall* PendingCallList::FindCallBySequence(uint32 sequence) {
  const ScopedLock lock(fMutex);
  PendingCall* retval = nullptr;

  for (int i = 0; i < this->Size(); ++i) {
    PendingCall* pc = fPendingCalls[i].get();
    if (sequence == pc->GetSequence()) {
      retval = pc;
      break;
    }
  }

  return retval;
}

void PendingCallList::Remove(PendingCall* pending, bool alsoDelete) {
  const ScopedLock lock(fMutex);

  fPendingCalls.remove(pending);
  if (alsoDelete) {
    delete pending;
  }

}

int PendingCallList::Size() const {
  return fPendingCalls.size();
}


ScopedPendingCall::ScopedPendingCall(PendingCallList& list, PendingCall* call)
    :  fList(list),  fCall(call)
{
  fList.Append(call);
}


ScopedPendingCall::~ScopedPendingCall() {
  fList.Remove(fCall);
}

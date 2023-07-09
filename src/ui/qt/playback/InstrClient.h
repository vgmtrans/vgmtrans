
#pragma once

#include "juce_events/juce_events.h"
#include "juce_core/juce_core.h"
#include "PendingCalls.h"

#define kPortNumber 0xBEEF
#define kPipeName "InstrServer"
#define kPipeTimeout 10000

class InstrClient: public juce::InterprocessConnection {
public:
  InstrClient();
  ~InstrClient() override;

  void connectionMade() override;
  void connectionLost() override;
  void messageReceived(const juce::MemoryBlock &message) override;

  bool sendSF2File(const juce::MemoryBlock& sf2Data);
  void sendSF2FileInBlocks(const juce::MemoryBlock& sf2Data);

  bool IsConnected() { return fIsConnected; };

private:
  bool fIsConnected;
  PendingCallList fPending;
};


#pragma once

#include <juce_core/juce_core.h>

enum InstrServerMessageCode
{
  kMessageError = 0,
  ResponseOk,
  ResponseError,
  SF2Start,
  SF2Content,
  SF2End
};

class InstrServerMessage {
public:
  InstrServerMessage(InstrServerMessageCode code, void * data, size_t numBytes);
  explicit InstrServerMessage(const juce::MemoryBlock& message);

  bool FromFile(juce::String filePath);
  InstrServerMessageCode GetCode();
  void* GetData();
  size_t GetDataLength();

  const juce::MemoryBlock& GetMemoryBlock() const {
    return fData;
  }

  InstrServerMessageCode code;
  juce::uint32 dataLength;
  juce::MemoryBlock fData;
};
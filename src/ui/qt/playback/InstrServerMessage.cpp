#include "InstrServerMessage.h"

using namespace juce;

namespace
{
  Atomic<uint32> sSequence{1};
}

InstrServerMessage::InstrServerMessage(InstrServerMessageCode code, void * data = nullptr, size_t numBytes = 0)
{
  auto code32 = static_cast<uint32>(code);
  fData.append(&code32, sizeof(uint32));
  uint32 sequence = ++sSequence;
  fData.append(&sequence, sizeof(uint32));
  if (data != nullptr && numBytes != 0) {
    fData.append(data, numBytes);
  }
}

bool InstrServerMessage::FromFile(String filePath) {
  juce::File file (filePath);

  if (! file.existsAsFile())
  {
    std::cerr << "File not found: " << filePath << std::endl;
    return false;
  }

  juce::FileInputStream inputStream (file);

  if (! inputStream.openedOk())
  {
    std::cerr << "Failed to open file: " << filePath << std::endl;
    return false;
  }

  inputStream.readIntoMemoryBlock (fData);
  return true;
}


InstrServerMessage::InstrServerMessage(const MemoryBlock& message)
{
  fData = message;
}

InstrServerMessageCode InstrServerMessage::GetCode() {
  if (fData.getSize() < sizeof(uint32)) {
    return kMessageError;
  }
  return static_cast<InstrServerMessageCode>((static_cast<uint32*>(fData.getData())[0]));
}

uint32 InstrServerMessage::GetSequence() {
  if (fData.getSize() < (2 * sizeof(uint32))) {
    return kMessageError;
  }
  return static_cast<uint32*>(fData.getData())[1];
}

void* InstrServerMessage::GetData() {
  if (fData.getSize() < 2*sizeof(uint32)) {
    return nullptr;
  }
  return static_cast<char*>(fData.getData()) + (2 * sizeof(uint32));
}

size_t InstrServerMessage::GetDataLength() {
  return fData.getSize() - (2*sizeof(uint32));
}
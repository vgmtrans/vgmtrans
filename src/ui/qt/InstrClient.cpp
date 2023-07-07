#include "InstrClient.h"
#include "InstrServerMessage.h"

#define BLOCK_SIZE (1024*512)

using namespace juce;

InstrClient::InstrClient()
    :  InterprocessConnection(false, 0xf2b49e2c)
      ,  fIsConnected(false)
{

}

InstrClient::~InstrClient()
{
  disconnect();
}

void InstrClient::connectionMade()
{
  DBG("InstrClient::connectionMade()");
  fIsConnected = true;
}

void InstrClient::connectionLost()
{
  DBG("InstrClient::connectionLost()");
  fIsConnected = false;
}

void InstrClient::messageReceived(const MemoryBlock& message)
{
  DBG("InstrClient::messageReceived()");
}

void InstrClient::sendSF2File(const juce::MemoryBlock& sf2Data)
{
  const auto sf2StartMsg = InstrServerMessage(InstrServerMessageCode::SF2Start, nullptr, 0);
  sendMessage(sf2StartMsg.GetMemoryBlock());

  const size_t totalSize = sf2Data.getSize();
  size_t offset = 0;

  while (offset < totalSize) {
    const size_t bytesToRead = juce::jmin((size_t)BLOCK_SIZE, totalSize - offset);

    const auto sf2ContentMsg = InstrServerMessage(
        InstrServerMessageCode::SF2Content,
        ((uint8*)sf2Data.getData()) + offset,
        bytesToRead
    );
    sendMessage(sf2ContentMsg.GetMemoryBlock());
    offset += bytesToRead;
  }

  const auto sf2EndMsg = InstrServerMessage(InstrServerMessageCode::SF2End, nullptr, 0);
  sendMessage(sf2EndMsg.GetMemoryBlock());
}

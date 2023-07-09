#include "InstrClient.h"
#include "InstrServerMessage.h"

#define BLOCK_SIZE (1024*512)

using namespace juce;

InstrClient::InstrClient()
    :  InterprocessConnection(false, 0xf2b49e2c)
      ,  fIsConnected(false)
{

}

InstrClient::~InstrClient() {
  disconnect();
}

void InstrClient::connectionMade() {
  DBG("InstrClient::connectionMade()");
  fIsConnected = true;
}

void InstrClient::connectionLost() {
  DBG("InstrClient::connectionLost()");
  fIsConnected = false;
}

void InstrClient::messageReceived(const MemoryBlock& message) {
  DBG("InstrClient::messageReceived()");
  InstrServerMessage msg(message);
  InstrServerMessageCode code = msg.GetCode();
  uint32 sequence = msg.GetSequence();

  PendingCall* pc = fPending.FindCallBySequence(sequence);
  if (pc) {
    // copy in the reply data and wake that thread up.
    pc->SetMemoryBlock(message);
    pc->Signal();

  }
  else {
    DBG("ERROR: Unexpected reply to call sequence #" + String(sequence));
  }
}

bool InstrClient::sendSF2File(const juce::MemoryBlock& sf2Data) {

  auto sf2SendMsg = InstrServerMessage(InstrServerMessageCode::SF2Send,
                                             (void*)sf2Data.getData(),
                                             sf2Data.getSize());

  uint32 sequence = sf2SendMsg.GetSequence();
  DBG("sequence " + juce::String(sequence));

  PendingCall pc(sequence);
  const ScopedPendingCall spc(fPending, &pc);

  sendMessage(sf2SendMsg.GetMemoryBlock());

  if (pc.Wait(3000)) {
    InstrServerMessage responseMsg(pc.GetMemoryBlock());
    InstrServerMessageCode responseCode = responseMsg.GetCode();
    if (responseCode == ResponseOk) {
      DBG("InstrClient received OK response after sending SoundFont file");
    } else {
      DBG("Error: InstrClient received response: " + String(responseCode));
    }
  } else {
    DBG("Error: Timed out waiting for response from instrument server after sending SoundFont file");
    return false;
  }
  return true;
}

void InstrClient::sendSF2FileInBlocks(const juce::MemoryBlock& sf2Data) {
  const auto sf2StartMsg = InstrServerMessage(InstrServerMessageCode::SF2StartBlockTransfer, nullptr, 0);
  sendMessage(sf2StartMsg.GetMemoryBlock());

  const size_t totalSize = sf2Data.getSize();
  size_t offset = 0;

  while (offset < totalSize) {
    const size_t bytesToRead = juce::jmin((size_t)BLOCK_SIZE, totalSize - offset);

    const auto sf2ContentMsg = InstrServerMessage(
        InstrServerMessageCode::SF2Block,
        ((uint8*)sf2Data.getData()) + offset,
        bytesToRead
    );
    sendMessage(sf2ContentMsg.GetMemoryBlock());
    offset += bytesToRead;
  }

  auto sf2EndMsg = InstrServerMessage(InstrServerMessageCode::SF2EndBlockTransfer, nullptr, 0);
  sendMessage(sf2EndMsg.GetMemoryBlock());
}

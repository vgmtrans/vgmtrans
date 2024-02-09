#include "VSTSequencePlayer.h"
#include "Root.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "SF2File.h"
//#include "InstrClient.h"
//#include "InstrServerMessage.h"

using namespace std;

#define MAX_CHANNELS 48

VSTSequencePlayer::VSTSequencePlayer() = default;

VSTSequencePlayer::~VSTSequencePlayer() {
  deviceManager.removeAudioCallback(this);
  deviceManager.closeAudioDevice();
  pluginPlayer.setProcessor(nullptr);
  pluginInstance = nullptr;
}

void VSTSequencePlayer::initialize() {
  juce::String error = deviceManager.initialiseWithDefaultDevices(0, 2);
}

bool VSTSequencePlayer::loadCollection(VGMColl *coll, std::function<void()> const& onCompletion) {

  // If we haven't loaded the VST yet, do it now.
  if (! pluginInstance) {
    if (!loadVST()) {
      return false;
    }
  }
  state.musicState = MusicState::Unloaded;

  auto bgLoadSF2 = [coll, this, onCompletion]() {
    sendSF2ToVST(coll);
    state.musicState = MusicState::Stopped;
    onCompletion();
  };
  std::thread myThread(bgLoadSF2);
  myThread.detach();

  prepMidiPlayback(coll->GetSeq());
  enqueueResetEvent();
  return true;
}

void VSTSequencePlayer::callAsyncIfNotInMessageThread(std::function<void()> fn) {
  auto mm = juce::MessageManager::getInstance();
  auto inMessageThread = mm->isThisTheMessageThread();
  if (! inMessageThread) {
    juce::MessageManager::callAsync(fn);
  } else {
    fn();
  }
}

// We wrap deviceManager calls in MessageManager::callAsync below because JUCE will complain if
// deviceManager calls execute outside the "Messaging Thread", and MessageManager::callAsync ensures
// that the block is run on the Messaging Thread. These methods may be invoked in the callback after
// an SF2 is loaded, which runs on a separate, thread, hence the need.
void VSTSequencePlayer::stop() {
  state.musicState = MusicState::Stopped;
  callAsyncIfNotInMessageThread([this](){
    deviceManager.removeAudioCallback(this);
  });
  state.eventOffset = 0;
  state.samplesOffset = 0;
}

void VSTSequencePlayer::pause() {
  state.musicState = MusicState::Paused;
  callAsyncIfNotInMessageThread([this](){
    deviceManager.removeAudioCallback(this);
  });
}

void VSTSequencePlayer::play() {
  callAsyncIfNotInMessageThread([this](){
    deviceManager.removeAudioCallback(this);
    deviceManager.addAudioCallback(this);
  });
  state.musicState = MusicState::Playing;
}

void VSTSequencePlayer::seek(int samples) {
  // Remember original music state and pause playback while we're processing the seek
  MusicState originalMusicState = state.musicState;
  state.musicState = MusicState::Paused;

  // Do a binary search to find the offset of the MidiEvent we're seeking to
  auto it = std::lower_bound(
      state.eventSampleOffsets.begin(),
      state.eventSampleOffsets.end(),
      samples
  );

  // If we didn't find the event, or it's the last, jump to the end and let the audio callback stop playback
  if (it == state.eventSampleOffsets.end()) {
    pRoot->AddLogItem(new LogItem("Could not seek to specified sample offset", LOG_LEVEL_WARN, "MusicPlayer"));
    state.samplesOffset = state.eventSampleOffsets.back();
    state.eventOffset = state.events.size();
    return;
  }

  // Calculate the offset of the event by doing some arithmetic on the iterator
  int index = std::distance(state.eventSampleOffsets.begin(), it);
  state.eventOffset = index;
  state.samplesOffset = state.eventSampleOffsets[index];

  // Iterate from the beginning of the sequence to the seek point. Record every relevant event
  // into the SeekChannelStates, overwriting them as we go so that they store the most recent
  // relevant midi events
  SeekChannelState channelStates[MAX_CHANNELS];
  vector<MidiEvent*> sysexEvents;
  for (size_t i = 0; i < state.eventOffset; i++) {
    MidiEvent* event = state.events[i];

    int chan = event->channel + (event->prntTrk->channelGroup * 16);
    if (chan >= MAX_CHANNELS) {
      continue;
    }

    ControllerEvent* controllerEvent = dynamic_cast<ControllerEvent*>(event);
    if (controllerEvent) {
      channelStates[chan].controlStates[controllerEvent->controlNum] = controllerEvent;
      continue;
    }
    ProgChangeEvent* programChangeEvent = dynamic_cast<ProgChangeEvent*>(event);
    if (programChangeEvent) {
      channelStates[chan].program = programChangeEvent;
      continue;
    }
    PitchBendEvent* pitchBendEvent = dynamic_cast<PitchBendEvent*>(event);
    if (pitchBendEvent) {
      channelStates[chan].pitchBend = pitchBendEvent;
      continue;
    }
    if (event->IsSysexEvent()) {
      sysexEvents.push_back(event);
      continue;
    }
    // NOTE: If we ever add Note Aftertouch or Channel Aftertouch events, we'll need to handle it here
  }

  // Start by adding a reset event that will cause our VST instrument to reset all channel states
  enqueueResetEvent();

  // Add the sysex events
  for (int i = 0; i < sysexEvents.size(); i++) {
    MidiEvent* sysexEvent = sysexEvents[i];
    juce::MidiMessage msg = convertToJuceMidiMessage(sysexEvent);
    seekMidiBuffer.addEvent(msg, 1);
  }

  // Iterate over all SeekChannelStates and add the MidiEvents to the seekMidiBuffer
  for (int i = 0; i < MAX_CHANNELS; i++) {
    SeekChannelState chanState = channelStates[i];

    // Add the program change, pitch bend, and channel after touch events if they exist
    for (const auto event :
         {chanState.program, chanState.pitchBend, chanState.channelAfterTouch}) {
      if (event == nullptr) {
        continue;
      }
      juce::MidiMessage msg = convertToChannelGroupMessage(event);
      seekMidiBuffer.addEvent(msg, 1);
    }

    // Iterate over controlStates and noteAfterTouch together
    for (const auto& chanStateMap : {chanState.controlStates, chanState.noteAfterTouch}) {
      for (const auto& pair : chanStateMap) {
        const auto controllerEvent = pair.second;
        juce::MidiMessage msg = convertToChannelGroupMessage(controllerEvent);
        seekMidiBuffer.addEvent(msg, 1);
      }
    }
  }

  state.musicState = originalMusicState;
}

void VSTSequencePlayer::setSongEndCallback(std::function<void()> callback) {
  songEndCallback = callback;
}

void VSTSequencePlayer::enqueueResetEvent() {
  juce::MidiMessage resetEvent(0xF0, 0x7D, 0x7F, 0xF7);
  seekMidiBuffer.addEvent(resetEvent, 0);
}

juce::FileSearchPath VSTSequencePlayer::getVSTSearchPath() {
  #if defined(Q_OS_MAC)
    juce::File appBundle = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File resourcesDir = appBundle.getChildFile("Contents/Resources");
    juce::File vstDirFile = resourcesDir.getChildFile("vst");
  #elif defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    juce::File vstDirFile = juce::File::getCurrentWorkingDirectory().getChildFile("vst");
  #endif

  juce::FileSearchPath vstDir(vstDirFile.getFullPathName());
  return vstDir;
}

bool VSTSequencePlayer::loadVST() {
  juce::String error;

  // Initialise the plugin format manager
  pluginFormatManager.addFormat(new juce::VST3PluginFormat());

  // Create a known plugin list
  juce::KnownPluginList pluginList;

  // Non-recursive scan doesn't seem to find the vst file on Windows.
  juce::PluginDirectoryScanner scanner (pluginList, *pluginFormatManager.getFormat(0), getVSTSearchPath(), true, juce::File());

  // Scan for new plugins
  juce::String pluginName;
  while (scanner.scanNextFile (true, pluginName)) {}

  // Choose the first available plugin
  jassert(pluginList.getNumTypes() > 0);
  auto pluginDesc = pluginList.getTypes()[0];
  // Create an instance of the plugin
  auto instance = pluginFormatManager.createPluginInstance(
      pluginDesc,
      deviceManager.getCurrentAudioDevice()->getCurrentSampleRate(),
      deviceManager.getCurrentAudioDevice()->getDefaultBufferSize(),
      error);

  // Check for error while loading the plugin
  if (instance == nullptr || !error.isEmpty()) {
    // handle error
    return false;
  }

  // Take ownership of the plugin instance
  pluginInstance = std::move(instance);

  // Prepare the plugin for playback
  double sampleRate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();
  int bufferSize = deviceManager.getCurrentAudioDevice()->getDefaultBufferSize();

  pluginInstance->setPlayConfigDetails(0,2, sampleRate,bufferSize);
  pluginInstance->prepareToPlay(sampleRate, bufferSize);

  pluginPlayer.setProcessor(pluginInstance.get());
  return true;
}

bool VSTSequencePlayer::sendSF2ToVST(VGMColl* coll) {
  SF2File* sf2 = coll->CreateSF2File();
  if (!sf2) {
    pRoot->AddLogItem(
        new LogItem("Failed to play collection as a soundfont file could not be produced.",
                    LOG_LEVEL_ERR, "VSTSequencePlayer"));
    return false;
  }

  auto rawSF2 = sf2->SaveToMem();
  auto sf2Size = sf2->GetSize();
  populateSF2MidiBuffer(rawSF2, sf2Size);

  delete[] rawSF2;
  return true;
}

// This reads n bytes from a buffer (where n is <= 7) and converts them into a n+1 size
// chunk of 7-bit bytes. The first byte stores the 8th bit of every subsequent byte
// starting from the 0th bit.
uint64_t VSTSequencePlayer::convertTo7BitMidiChunk(uint8_t* buf, uint8_t n) {
  uint64_t result = 0;
  uint64_t firstByte = 0; // To store the combined 8th bits

  // Iterate through the first n bytes of buf
  for (uint8_t i = 0; i < n; ++i) {
    // Extract the 8th bit and shift it into its position in the first byte
    firstByte |= ((uint64_t)(buf[i] & 0x80) >> 7) << i;

    // Clear the 8th bit of the current byte and place it in the result
    result |= ((uint64_t)(buf[i] & 0x7F)) << (8 * ((n - 1) - i));
  }

  // Combine the first byte with the rest of the bytes in result
  result |= firstByte << (8 * 7);
  return result;
}

std::vector<uint8_t> VSTSequencePlayer::encode6BitVariableLengthQuantity(uint32_t value) {
  std::vector<uint8_t> encodedBytes;
  encodedBytes.reserve(6);

  // Start with the least significant 7 bits of value
  uint64_t buffer = value & 0x3F;

  // Shift value to process next 7 bits
  while (value >>= 6) {
    // Move to the next 7 bits block and set the continuation bit
    buffer <<= 8;
    buffer |= ((value & 0x3F) | 0x40);
  }

  // Push bytes to the vector
  while (true) {
    encodedBytes.emplace_back(buffer & 0xFF);  // Push the least significant byte
    if (buffer & 0x40) {
      buffer >>= 8;  // Move to the next byte if the continuation bit is set
    } else {
      return encodedBytes;  // Last byte reached, exit loop
    }
  }
}

juce::MidiMessage VSTSequencePlayer::createSysExMessage(
    uint8_t commandByte,
    uint8_t* data,
    uint32_t dataSize,
    uint8_t* eventBuffer
) {
  int i = 0;
  eventBuffer[i++] = 0xF0;
  eventBuffer[i++] = 0x7D;
  eventBuffer[i++] = commandByte;
  memcpy(eventBuffer+i, data, dataSize);
  i += dataSize;
  eventBuffer[i++] = 0xF7;
  return juce::MidiMessage(eventBuffer, i, 0);
}

void VSTSequencePlayer::populateSF2MidiBuffer(uint8_t* rawSF2, uint32_t sf2Size) {
  // If an SF2 send is already queued, don't mess with the midi buffer
  if (readyToSendSF2) {
    return;
  }

  const size_t maxPacketSize = 64*1024 - 16; //65536-8;
  const size_t chunkSize = 8; // Size of chunks to process
  const size_t chunkDataSize = 7;

  // Calculate the number of full chunks and the size of the last chunk (if any)
  const size_t numFullChunks = sf2Size / chunkDataSize;
  const size_t lastChunkSize = sf2Size % chunkDataSize;
  const size_t chunksPerPacket = maxPacketSize / chunkSize;

  auto packetBuf = new uint8_t[maxPacketSize];
  auto eventBuf = new uint8_t[maxPacketSize];

  auto test = encode6BitVariableLengthQuantity(0x1FFFFF);

  // send a sysex message indicating the begin of the SF2 send and its total size
  auto encodedSize = encode6BitVariableLengthQuantity(sf2Size);
  auto startSF2SendEvent = createSysExMessage(
    0x10,
    encodedSize.data(),
    encodedSize.size(),
    eventBuf);

  sendSF2MidiBuffer.addEvent(startSF2SendEvent, 0);

  int chunkNum = 0;
  int packetIx = 0;
  for (size_t i = 0; i < numFullChunks; ++i) {
    // Process each 8-byte chunk
    uint64_t processedChunk = convertTo7BitMidiChunk(rawSF2 + i * chunkDataSize, chunkDataSize);

    // Add the processed chunk to the packet data
    for (int j = 7; j >= 0; --j) {
      packetBuf[packetIx++] = (processedChunk >> (8 * j)) & 0xFF;
    }
    chunkNum += 1;
    if (chunkNum == chunksPerPacket) {
      // Packet is full. Store it in a MidiMessage and add it to the midiBuffer
      auto packet = createSysExMessage(
          0x11,
          packetBuf,
          packetIx,
          eventBuf);
      sendSF2MidiBuffer.addEvent(packet, 0);
      packetIx = 0;
      chunkNum = 0;
    }
  }

  // Process the final partial data chunk if it exists
  if (lastChunkSize > 0) {
    uint64_t processedChunk = convertTo7BitMidiChunk(rawSF2 + numFullChunks * chunkDataSize, lastChunkSize);
    for (int j = 7; j >= 0; --j) {
      packetBuf[packetIx++] = (processedChunk >> (8 * j)) & 0xFF;
    }
  }

  // Send the last packet if there's remaining data
  if (packetIx > 0) {
    auto packet = createSysExMessage(
        0x11,
        packetBuf,
        packetIx,
        eventBuf);
    sendSF2MidiBuffer.addEvent(packet, 0);
  }
  readyToSendSF2 = true;
  delete[] packetBuf;
  delete[] eventBuf;
}

void VSTSequencePlayer::clearState() {
  state.events.clear();
  state.eventSampleOffsets.clear();
  state.eventOffset = 0;
  state.samplesOffset = 0;
  state.sampleRate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();
}

bool VSTSequencePlayer::prepMidiPlayback(VGMSeq* seq) {
  clearState();

  // Convert the VGMSeq to midi, then allocate a vector to hold every midi event in the sequence
  MidiFile* midiFile = seq->ConvertToMidi();
  state.midiFile.reset(midiFile);

  size_t reserveSize = 0;
  for (size_t i=0; i< midiFile->aTracks.size(); i++) {
    reserveSize += midiFile->aTracks[i]->aEvents.size();
  }
  state.events.reserve(reserveSize);

  // Copy every midi event from every track into the vector
  for (size_t i=0; i< midiFile->aTracks.size(); i++) {
    state.events.insert(
        state.events.end(),
        midiFile->aTracks[i]->aEvents.begin(),
        midiFile->aTracks[i]->aEvents.end()
    );
  }

  //Add global events
  midiFile->globalTranspose = 0;
  state.events.insert(
      state.events.end(),
      midiFile->globalTrack.aEvents.begin(),
      midiFile->globalTrack.aEvents.end()
  );

  // If there are no events, delete and return. Otherwise store a reference to the midi file
  // so that we can delete it later
  if (state.events.empty()) {
    state.midiFile.reset();
    return false;
  }

  //Sort all the events by priority then absolute time in ticks
  stable_sort(state.events.begin(), state.events.end(), PriorityCmp());
  stable_sort(state.events.begin(), state.events.end(), AbsTimeCmp());

  // Calculate sample offset times for all midi events
  state.eventSampleOffsets = generateEventSampleTimes(state.events, midiFile->GetPPQN());
  return true;
}

std::vector<int> VSTSequencePlayer::generateEventSampleTimes(vector<MidiEvent*>& events, int ppqn) const {

  // Set the default milliseconds per tick, derived from the midi file's PPQN - pulses (ticks) per
  // quarter and the default starting tempo of 120bpm.
  int microsPerQuarter = 500000;
  double millisPerTick = static_cast<double>(microsPerQuarter)/ ppqn / 1000.0;
  double sampleRate = state.sampleRate;

  std::vector<int> realTimeEventOffsets(events.size());

  int lastTempoAbsTimeInTicks = 0;
  int lastTempoAbsTimeInSamples = 0;

  for(size_t i = 0; i < events.size(); ++i) {
    MidiEvent* event = events[i];

    if (event->GetEventType() == MidiEventType::MIDIEVENT_TEMPO) {

      // When we encounter a tempo event, we record the time offset of the event in both samples and ticks.
      // Then, to calculate the sample time offset of any subsequent event, we substitute the sample offset
      // of the most recent tempo event with the number of ticks that led up to that tempo event. Then, we take
      // the remaining ticks and multiply them by millisPerTick. Add these together and we're done.
      lastTempoAbsTimeInSamples = ((event->AbsTime - lastTempoAbsTimeInTicks) * millisPerTick) + lastTempoAbsTimeInSamples;
      lastTempoAbsTimeInTicks = event->AbsTime;

      TempoEvent* tempoEvent = static_cast<TempoEvent*>(event);
      microsPerQuarter = static_cast<int>(tempoEvent->microSecs);
      millisPerTick = static_cast<double>(microsPerQuarter)/ ppqn / 1000.0;
    }
    double eventTimeInMs = ((event->AbsTime - lastTempoAbsTimeInTicks) * millisPerTick) + lastTempoAbsTimeInSamples;
    realTimeEventOffsets[i] = static_cast<int>(eventTimeInMs * sampleRate / 1000.0);
  }

  return realTimeEventOffsets;
}

juce::MidiMessage VSTSequencePlayer::convertToChannelGroupMessage(MidiEvent* event) const {
  vector<uint8_t> eventData;
  event->WriteEvent(eventData, event->AbsTime);
  // Remove the delta time byte (which should be 0x00)
  eventData.erase(eventData.begin());
  // Wrap the original event around a Sysex Event with arbitrary type 0x7D. First data byte is channel group
  uint8_t sysexBegin[] = { 0xF0, 0x7D, static_cast<uint8_t>(event->prntTrk->channelGroup)};//, 0xF7};
  // Next add the original event
  eventData.insert(eventData.begin(), sysexBegin, sysexBegin + sizeof(sysexBegin));
  // Add the Sysex end marker byte
  eventData.push_back(0xF7);
  return juce::MidiMessage(eventData.data(), eventData.size(), 0);
}

juce::MidiMessage VSTSequencePlayer::convertToJuceMidiMessage(MidiEvent* event) const {
  vector<uint8_t> eventData;
  event->WriteEvent(eventData, event->AbsTime);
  return juce::MidiMessage(eventData.data()+1, eventData.size()-1, 0);
}

// Fill up a JUCE MidiBuffer for a specified duration of playback
void VSTSequencePlayer::populateMidiBuffer(juce::MidiBuffer& midiBuffer, int samples) {
  while (
      (state.eventOffset < state.eventSampleOffsets.size()) &&
      (state.eventSampleOffsets[state.eventOffset] < (state.samplesOffset + samples))
  ) {
    MidiEvent* midiEvent = state.events[state.eventOffset];
    int offset = state.eventSampleOffsets[state.eventOffset] - state.samplesOffset;

    // Ignore Meta events because they will be filtered by JUCE (and are irrelevant anyway).
    if (midiEvent->IsMetaEvent() ||
        midiEvent->GetEventType() == MidiEventType::MIDIEVENT_MARKER) {
      state.eventOffset++;
      continue;
    }

    // If it's a sysex event, pass it as is
    else if (midiEvent->IsSysexEvent()) {
      juce::MidiMessage msg = convertToJuceMidiMessage(midiEvent);
      midiBuffer.addEvent(msg, offset);
    } else if (midiEvent->GetEventType() == MidiEventType::MIDIEVENT_GLOBALTRANSPOSE) {
      // Global transpose events are not actual MIDI events but instead change sequence-level state
      // when written, affecting all subsequent notes. As such, we call WriteEvent() but don't pass
      // the event into the buffer as there is no data to send.
      vector<uint8_t> eventData;
      midiEvent->WriteEvent(eventData, midiEvent->AbsTime);
    } else {
      // Otherwise we wrap the event in a sysex event of our creation. We do this because we need
      // to pass channelGroup information to allow for > 16 midi channel playback. Other methods of
      // sending this (preceding every event with an extra sysex event) do not work because JUCE
      // reorders the events in the MidiBuffer passed via processBlock under the hood.
      juce::MidiMessage msg = convertToChannelGroupMessage(midiEvent);
      midiBuffer.addEvent(msg, offset);
    }

    state.eventOffset++;
  }
//  DBG("PRINTING MIDI BUFFER at sampleOffset" + juce::String(state.samplesOffset));
//  for (auto i=midiBuffer.begin(); i != midiBuffer.end(); i++) {
//    auto test = *i;
//    DBG(test.getMessage().getDescription());
//  }
}

void VSTSequencePlayer::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                         int numInputChannels,
                                                         float* const* outputChannelData,
                                                         int numOutputChannels,
                                                         int numSamples,
                                                         const juce::AudioIODeviceCallbackContext& context)    {
  // Prepare an audio buffer
  juce::AudioSampleBuffer buffer(outputChannelData, numOutputChannels, numSamples);

  // Clear the output
  buffer.clear();
  midiBuffer.clear();

  // If the sendSF2MidiBuffer is non-empty, we'll exclusively process its messages in this
  // callback iteration.
  if (readyToSendSF2 && !sendSF2MidiBuffer.isEmpty()) {
    pluginInstance->processBlock(buffer, sendSF2MidiBuffer);
    sendSF2MidiBuffer.clear();
    readyToSendSF2 = false;
    return;
  }

  // If the seekMidiBuffer is non-empty, we'll exclusively process its messages in this
  // callback iteration. After we clear the buffer, playback will commence next iteration.
  if (!seekMidiBuffer.isEmpty()) {
    pluginInstance->processBlock(buffer, seekMidiBuffer);
    seekMidiBuffer.clear();
    return;
  }

  // If we're not playing, return without processing any samples
  if (state.musicState != MusicState::Playing) {
    return;
  }

  // If we've reached the end of the song, set state to stopped and execute song end callback
  if (state.eventOffset >= state.events.size()) {
    state.musicState = MusicState::Stopped;
    songEndCallback();
    return;
  }

  // Populate MIDI buffer for this block
  populateMidiBuffer(midiBuffer, numSamples);

  // Let the processor prepare for this block
  pluginInstance->processBlock(buffer, midiBuffer);
  state.samplesOffset += numSamples;
}

void VSTSequencePlayer::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
  pluginPlayer.audioDeviceAboutToStart(device);
}

void VSTSequencePlayer::audioDeviceStopped()
{
  pluginPlayer.audioDeviceStopped();
}

#include "NewSequencePlayer.h"
#include "Root.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "SF2File.h"
#include "InstrClient.h"
#include "InstrServerMessage.h"
//#include "../../../external/juicysfplugin/src/InstrServerMessage.h"

using namespace std;

#define MAX_CHANNELS 48

NewSequencePlayer::NewSequencePlayer() = default;

NewSequencePlayer::~NewSequencePlayer() {
  delete state.midiFile;
}

void NewSequencePlayer::initialize() {
  juce::String error = deviceManager.initialiseWithDefaultDevices(0, 2);
}

void NewSequencePlayer::shutdown() {
  client->disconnect();
  deviceManager.removeAudioCallback(this);
  deviceManager.closeAudioDevice();
  pluginPlayer.setProcessor(nullptr);
  pluginInstance = nullptr;
}

bool NewSequencePlayer::loadCollection(VGMColl *coll, std::function<void()> const& onCompletion) {

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

void NewSequencePlayer::callAsyncIfNotInMessageThread(std::function<void()> fn) {
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
void NewSequencePlayer::stop() {
  state.musicState = MusicState::Stopped;
  callAsyncIfNotInMessageThread([this](){
    deviceManager.removeAudioCallback(this);
  });
  state.eventOffset = 0;
  state.samplesOffset = 0;
}

void NewSequencePlayer::pause() {
  state.musicState = MusicState::Paused;
  callAsyncIfNotInMessageThread([this](){
    deviceManager.removeAudioCallback(this);
  });
}

void NewSequencePlayer::play() {
  callAsyncIfNotInMessageThread([this](){
    deviceManager.removeAudioCallback(this);
    deviceManager.addAudioCallback(this);
  });
  state.musicState = MusicState::Playing;
}

void NewSequencePlayer::seek(int samples) {
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
    pRoot->AddLogItem(new LogItem(L"Could not seek to specified sample offset", LOG_LEVEL_WARN, L"MusicPlayer"));
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

void NewSequencePlayer::setSongEndCallback(std::function<void()> callback) {
  songEndCallback = callback;
}

void NewSequencePlayer::enqueueResetEvent() {
  juce::MidiMessage resetEvent(0xF0, 0x7D, 0x7F, 0xF7);
  seekMidiBuffer.addEvent(resetEvent, 0);
}

bool NewSequencePlayer::loadVST() {
  juce::String error;

  // Initialise the plugin format manager
  pluginFormatManager.addFormat(new juce::VST3PluginFormat());

  // Create a known plugin list
  juce::KnownPluginList pluginList;

  juce::File vstDirFile = juce::File::getCurrentWorkingDirectory().getChildFile("vst");
  juce::FileSearchPath vstDir(vstDirFile.getFullPathName());
  // Non-recursive scan doesn't seem to find the vst file on Windows.
  juce::PluginDirectoryScanner scanner (pluginList, *pluginFormatManager.getFormat(0), vstDir, true, juce::File());

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

bool NewSequencePlayer::sendSF2ToVST(VGMColl* coll) {
  client = make_unique<InstrClient>();

  juce::String hostname("127.0.0.1");

  if (! client->connectToSocket(hostname, kPortNumber, 5*1000)) {
//  if (! client->connectToPipe(kPipeName, kPipeTimeout))  {
    return false;
  }
  SF2File* sf2 = coll->CreateSF2File();
  if (!sf2) {
    pRoot->AddLogItem(
        new LogItem(L"Failed to play collection as a soundfont file could not be produced.",
                    LOG_LEVEL_ERR, L"SequencePlayer"));
    return false;
  }

  auto rawSF2 = sf2->SaveToMem();
  auto memblk = juce::MemoryBlock(rawSF2, sf2->GetSize());
  client->sendSF2File(memblk);
  delete[] rawSF2;
  return true;
}

void NewSequencePlayer::clearState() {
  delete state.midiFile;
  state.events.clear();
  state.eventSampleOffsets.clear();
  state.eventOffset = 0;
  state.samplesOffset = 0;
  state.sampleRate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();
}

bool NewSequencePlayer::prepMidiPlayback(VGMSeq* seq) {
  clearState();

  // Convert the VGMSeq to midi, then allocate a vector to hold every midi event in the sequence
  MidiFile* midiFile = seq->ConvertToMidi();
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
    delete midiFile;
    return false;
  } else {
    state.midiFile = midiFile;
  }

  //Sort all the events by priority then absolute time in ticks
  stable_sort(state.events.begin(), state.events.end(), PriorityCmp());
  stable_sort(state.events.begin(), state.events.end(), AbsTimeCmp());

  // Calculate sample offset times for all midi events
  state.eventSampleOffsets = generateEventSampleTimes(state.events, midiFile->GetPPQN());
  return true;
}

std::vector<int> NewSequencePlayer::generateEventSampleTimes(vector<MidiEvent*>& events, int ppqn) const {

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

juce::MidiMessage NewSequencePlayer::convertToChannelGroupMessage(MidiEvent* event) const {
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

juce::MidiMessage NewSequencePlayer::convertToJuceMidiMessage(MidiEvent* event) const {
  vector<uint8_t> eventData;
  event->WriteEvent(eventData, event->AbsTime);
  return juce::MidiMessage(eventData.data()+1, eventData.size()-1, 0);
}

// Fill up a JUCE MidiBuffer for a specified duration of playback
void NewSequencePlayer::populateMidiBuffer(juce::MidiBuffer& midiBuffer, int samples) {
  while (
      (state.eventOffset < state.eventSampleOffsets.size()) &&
      (state.eventSampleOffsets[state.eventOffset] < (state.samplesOffset + samples))
  ) {
    MidiEvent* midiEvent = state.events[state.eventOffset];
    int offset = state.eventSampleOffsets[state.eventOffset] - state.samplesOffset;

    // Ignore Meta events because they will be filtered by JUCE (and are irrelevant anyway).
    if (midiEvent->IsMetaEvent() ||
        midiEvent->GetEventType() == MidiEventType::MIDIEVENT_MARKER ||
        midiEvent->GetEventType() == MidiEventType::MIDIEVENT_GLOBALTRANSPOSE) {
      state.eventOffset++;
      continue;
    }

    // If it's a sysex event, pass it as is
    else if (midiEvent->IsSysexEvent()) {
      juce::MidiMessage msg = convertToJuceMidiMessage(midiEvent);
      midiBuffer.addEvent(msg, offset);
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

void NewSequencePlayer::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
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

  // If the seekMidiBuffer is non-empty, we'll exclusively process its messages in this
  // callback iteration. After we clear the buffer, playback will commence next iteration.
  if (! seekMidiBuffer.isEmpty()) {
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

void NewSequencePlayer::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
  pluginPlayer.audioDeviceAboutToStart(device);
}

void NewSequencePlayer::audioDeviceStopped()
{
  pluginPlayer.audioDeviceStopped();
}

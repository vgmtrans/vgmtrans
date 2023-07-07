#include "NewSequencePlayer.h"
#include "Root.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "SF2File.h"
#include "InstrClient.h"
#include "InstrServerMessage.h"
//#include "../../../external/juicysfplugin/src/InstrServerMessage.h"

using namespace std;

NewSequencePlayer::NewSequencePlayer() {
  juce::String error = deviceManager.initialiseWithDefaultDevices(0, 2);
}

NewSequencePlayer::~NewSequencePlayer() {
  deviceManager.removeAudioCallback(this);
  deviceManager.closeAudioDevice();
  pluginPlayer.setProcessor(nullptr);
  pluginInstance = nullptr;
}

void NewSequencePlayer::initialize() {
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

bool sendSF2ToVST(VGMColl* coll) {
  InstrClient* client = new InstrClient();

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
}

void NewSequencePlayer::resetState() {
  state.events.clear();
  state.eventSampleOffsets.clear();
  state.eventOffset = 0;
  state.samplesOffset = 0;
  state.sampleRate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();
}

bool NewSequencePlayer::prepMidiPlayback(VGMSeq* seq) {
  resetState();

  MidiFile* pMidi = seq->ConvertToMidi();
  size_t reserveSize = 0;
  for (size_t i=0; i<pMidi->aTracks.size(); i++) {
    reserveSize += pMidi->aTracks[i]->aEvents.size();
  }
  state.events.reserve(reserveSize);

  for (size_t i=0; i<pMidi->aTracks.size(); i++) {
    state.events.insert(
        state.events.end(),
        pMidi->aTracks[i]->aEvents.begin(),
        pMidi->aTracks[i]->aEvents.end()
    );
  }

  //Add global events
  pMidi->globalTranspose = 0;
  state.events.insert(
      state.events.end(),
      pMidi->globalTrack.aEvents.begin(),
      pMidi->globalTrack.aEvents.end()
  );

  if (state.events.size() == 0) {
    delete pMidi;
//    musicplayer.bPlaying = false;
//    musicplayer.bPaused = false;
//    musicplayer.ReleaseAllKeys();
//    pMainFrame->UIEnable(ID_STOP, 0);
//    pMainFrame->UIEnable(ID_PAUSE, 0);
    return true;
  }

  //Sort all the events by priority then absolute time in ticks
  stable_sort(state.events.begin(), state.events.end(), PriorityCmp());
  stable_sort(state.events.begin(), state.events.end(), AbsTimeCmp());

  // Calculate sample offset times for all midi events
  state.eventSampleOffsets = generateEventSampleTimes(state.events, pMidi->GetPPQN());
}

std::vector<uint64_t> NewSequencePlayer::generateEventSampleTimes(vector<MidiEvent*>& events, int ppqn) {

  // Set the default milliseconds per tick, derived from the midi file's PPQN - pulses (ticks) per
  // quarter and the default starting tempo of 120bpm.
  int microsPerQuarter = 500000;
  double millisPerTick = static_cast<double>(microsPerQuarter)/ ppqn / 1000.0;
  double sampleRate = state.sampleRate;

  std::vector<uint64_t> realTimeEventOffsets(events.size());

  uint32_t lastTempoAbsTimeInTicks = 0;
  uint64_t lastTempoAbsTimeInSamples = 0;

  for(size_t i = 0; i < events.size(); ++i) {
    MidiEvent* event = events[i];

    if (event->GetEventType() == MidiEventType::MIDIEVENT_TEMPO) {

      lastTempoAbsTimeInSamples = ((event->AbsTime - lastTempoAbsTimeInTicks) * millisPerTick) + lastTempoAbsTimeInSamples;
      lastTempoAbsTimeInTicks = event->AbsTime;

      TempoEvent* tempoEvent = static_cast<TempoEvent*>(event);
      microsPerQuarter = static_cast<int>(tempoEvent->microSecs);
      millisPerTick = static_cast<double>(microsPerQuarter)/ ppqn / 1000.0;
    }
    double eventTimeInMs = ((event->AbsTime - lastTempoAbsTimeInTicks) * millisPerTick) + lastTempoAbsTimeInSamples;
    realTimeEventOffsets[i] = static_cast<uint64_t>(eventTimeInMs * sampleRate / 1000.0);
  }

  return realTimeEventOffsets;
}

bool NewSequencePlayer::playCollection(VGMColl *coll) {
  if (! pluginInstance) {
    if (!loadVST()) {
      return false;
    }
  }

  if (playedNotes % 2 == 0) {
    sendSF2ToVST(coll);

      std::this_thread::sleep_for (std::chrono::milliseconds (1000));

  } else {
    prepMidiPlayback(coll->GetSeq());
    deviceManager.removeAudioCallback(this);
    deviceManager.addAudioCallback(this);
  }

//  auto myFunction = [this]() {
//    std::this_thread::sleep_for (std::chrono::milliseconds (2000));
//    deviceManager.removeAudioCallback(this);
//    deviceManager.addAudioCallback(this);
//  };
//
//  std::thread myThread(myFunction);
//  myThread.detach();
  playedNotes++;
  return true;
}

void NewSequencePlayer::stop() {
  deviceManager.removeAudioCallback(this);
}

juce::MidiMessage NewSequencePlayer::convertToChannelGroupMessage(MidiEvent* event) {
  vector<uint8_t> eventData;
  event->WriteEvent(eventData, event->AbsTime);
  // Remove the delta time byte (which should be 0x00)
  eventData.erase(eventData.begin());
  // Wrap the original event around a Sysex Event with arbitrary type 0xBF. First data byte is channel group
  uint8_t sysexBegin[] = { 0xF0, 0xBF, static_cast<uint8_t>(event->prntTrk->channelGroup)};//, 0xF7};
  // Next add the original event
  eventData.insert(eventData.begin(), sysexBegin, sysexBegin + sizeof(sysexBegin));
  // Add the Sysex end marker byte
  eventData.push_back(0xF7);
  return juce::MidiMessage(eventData.data(), eventData.size(), 0);
}

juce::MidiMessage NewSequencePlayer::convertToJuceMidiMessage(MidiEvent* event) {
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
    uint64_t offset = state.eventSampleOffsets[state.eventOffset] - state.samplesOffset;

    // Ignore Meta events because they will be filtered by JUCE (and are irrelevant anyway).
    if (midiEvent->IsMetaEvent()) {
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
      // reorders the events under the hood.
      juce::MidiMessage msg = convertToChannelGroupMessage(midiEvent);
      midiBuffer.addEvent(msg, offset);
    }


    state.eventOffset++;
  }
}

void NewSequencePlayer::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                      int numInputChannels,
                                      float* const* outputChannelData,
                                      int numOutputChannels,
                                      int numSamples,
                                      const juce::AudioIODeviceCallbackContext& context)    {
  // Create a MIDI buffer for this block
  midiBuffer.clear();
  populateMidiBuffer(midiBuffer, numSamples);

  // Prepare an audio buffer
  juce::AudioSampleBuffer buffer(outputChannelData, numOutputChannels, numSamples);

  // Clear the output
  buffer.clear();

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

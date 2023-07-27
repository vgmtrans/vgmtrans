#pragma once

#include <QObject>
#include <QTimer>

#include "juce_audio_basics/juce_audio_basics.h"
#include "juce_audio_devices/juce_audio_devices.h"
#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_audio_utils/players/juce_AudioProcessorPlayer.h"
#include "InstrClient.h"

class VGMColl;
class VGMSeq;
class MidiEvent;
class MidiFile;

enum MusicState {
  Unloaded,
  Stopped,
  Paused,
  Playing
};

struct PlaybackState {
  std::unique_ptr<MidiFile> midiFile;
  std::vector<MidiEvent*> events;
  std::vector<int> eventSampleOffsets;
  uint32_t samplesOffset;
  uint32_t eventOffset;
  double sampleRate;
  MusicState musicState = MusicState::Unloaded;
};

// SeekChannelState will hold all of the relevant MidiEvents needed to define the state at
// a given time offset
struct SeekChannelState {
  std::map<uint8_t, MidiEvent*> controlStates;
  MidiEvent* program = nullptr;
  MidiEvent* pitchBend = nullptr;
  MidiEvent* channelAfterTouch = nullptr;
  std::map<uint8_t, MidiEvent*> noteAfterTouch;
};

class NewSequencePlayer : public QObject, public juce::AudioIODeviceCallback {
public:
  NewSequencePlayer();
  ~NewSequencePlayer();

  void initialize();

  /**
   * Loads a VGMColl for playback
   * @param collection
   * @return true if data was loaded correctly
   */
  bool loadCollection(VGMColl* coll, std::function<void()> const& onCompletion = [](){});
  void stop();
  void pause();
  void play();
  void seek(int samples);

  bool isPlaying() const {
    return state.musicState == MusicState::Playing;
  }

  int elapsedSamples() const {
    return state.samplesOffset;
  };

  int totalSamples() const {
    return state.eventSampleOffsets.back();
  };

  void setSongEndCallback(std::function<void()> callback);

private:
  juce::FileSearchPath getVSTSearchPath();
  void callAsyncIfNotInMessageThread(std::function<void()> fn);
  void enqueueResetEvent();
  bool loadVST();
  void clearState();
  bool prepMidiPlayback(VGMSeq* seq);
  bool sendSF2ToVST(VGMColl* coll);
  std::vector<int> generateEventSampleTimes(std::vector<MidiEvent*>& events, int ppqn) const;
  juce::MidiMessage convertToChannelGroupMessage(MidiEvent* event) const;
  juce::MidiMessage convertToJuceMidiMessage(MidiEvent* event) const;
  void populateMidiBuffer(juce::MidiBuffer& midiBuffer, int samples);

  // juce::AudioIODeviceCallback overrides
  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override;
  void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                        int numInputChannels,
                                        float* const* outputChannelData,
                                        int numOutputChannels,
                                        int numSamples,
                                        const juce::AudioIODeviceCallbackContext& context) override;

private:
  PlaybackState state;

  juce::MidiBuffer midiMessages;
  std::mutex midiMutex;

  std::unique_ptr<InstrClient> client;

  juce::MidiBuffer midiBuffer;
  juce::MidiBuffer seekMidiBuffer;

  juce::ScopedJuceInitialiser_GUI juceInitialiser;
  juce::AudioDeviceManager deviceManager;
  juce::AudioProcessorPlayer pluginPlayer;
  std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
  juce::AudioPluginFormatManager pluginFormatManager;

  std::function<void()> songEndCallback;
};
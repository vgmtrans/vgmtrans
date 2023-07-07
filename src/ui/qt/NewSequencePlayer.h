#pragma once

#include <QObject>
#include <QTimer>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/players/juce_AudioProcessorPlayer.h>

class VGMColl;
class VGMSeq;
class MidiEvent;

struct PlaybackState {
  std::vector<MidiEvent*> events;
  std::vector<uint64_t> eventSampleOffsets;
  uint32_t samplesOffset;
  uint32_t eventOffset;
  double sampleRate;
};

class NewSequencePlayer : public QObject, public juce::AudioIODeviceCallback {
  Q_OBJECT
public:
  static auto &the() {
    static NewSequencePlayer instance;
    return instance;
  }


  NewSequencePlayer(const NewSequencePlayer &) = delete;
  NewSequencePlayer &operator=(const NewSequencePlayer &) = delete;
  NewSequencePlayer(NewSequencePlayer &&) = delete;
  NewSequencePlayer &operator=(NewSequencePlayer &&) = delete;

  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override;

  void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                        int numInputChannels,
                                        float* const* outputChannelData,
                                        int numOutputChannels,
                                        int numSamples,
                                        const juce::AudioIODeviceCallbackContext& context) override;

  ~NewSequencePlayer();

  /**
   * Toggles the status of the player
   */
//  void toggle();
  /**
   * Stops the player (unloads resources)
   */
//  void stop();
  /**
   * Moves the player position
   * @param position relative to song start
   */
//  void seek(int position);

  /**
   * Checks whether the player is playing
   * @return true if playing
   */
//  [[nodiscard]] bool playing() const;
  /**
   * The number of MIDI ticks elapsed since the player was started
   * @return number of ticks relative to song start
   */
//  [[nodiscard]] int elapsedTicks() const;
  /**
   * The total number of MIDI ticks in the song
   * @return total ticks in the song
   */
//  [[nodiscard]] int totalTicks() const;

  /**
   * Returns the title of the song currently playing
   * @return the song title
   */
//  [[nodiscard]] QString songTitle() const;

  /**
   * Loads a VGMColl for playback
   * @param collection
   * @return true if data was loaded correctly
   */
  bool playCollection(VGMColl* collection);
  void stop();
  void sendMidiMessage(const juce::MidiMessage& message);
  void initialize();
  bool loadVST();

private:
  PlaybackState state;
  void resetState();
  bool prepMidiPlayback(VGMSeq* seq);
  std::vector<uint64_t> generateEventSampleTimes(std::vector<MidiEvent*>& events, int ppqn);
  juce::MidiMessage convertToChannelGroupMessage(MidiEvent* event);
  juce::MidiMessage convertToJuceMidiMessage(MidiEvent* event);

  void populateMidiBuffer(juce::MidiBuffer& midiBuffer, int samples);

//signals:
//  void statusChange(bool playing);
//  void playbackPositionChanged(int current, int max);

private:
  NewSequencePlayer();

  VGMColl *m_active_vgmcoll{};

  QTimer *m_seekupdate_timer{};
  QString m_song_title{};

  int playedNotes = 0;
  juce::MidiBuffer midiMessages;
  std::mutex midiMutex;

  juce::MidiBuffer midiBuffer;


  juce::ScopedJuceInitialiser_GUI juceInitialiser;
  juce::AudioDeviceManager deviceManager;
  juce::AudioProcessorPlayer pluginPlayer;
  std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
  juce::AudioPluginFormatManager pluginFormatManager;

};
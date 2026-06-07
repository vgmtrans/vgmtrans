/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMSeq.h"

#include "base/Types.h"
#include "Format.h"
#include "Helper.h"
#include "Options.h"
#include "Root.h"
#include "SeqEvent.h"
#include "SeqSlider.h"

#include <climits>
#include <ranges>
#include <vector>

VGMSeq::VGMSeq(const std::string &format, RawFile *file, u32 offset, u32 length, std::string name)
    : VGMFile(format, file, offset, length, std::move(name)),
      midi(nullptr),
      nNumTracks(0),
      readMode(READMODE_ADD_TO_UI),
      tempoBPM(0),
      time(0),
      bLoadTickByTick(false),
      bIncTickAfterProcessingTracks(true),
      initialTempoBPM(120),
      m_ppqn(0),
      m_initial_volume(100),                    // GM standard (dls1 spec p16)
      m_initial_expression(127),             //''
      m_initial_reverb_level(40),                  // GM standard
      m_initial_pitch_bend_range_cents(200), // GM standard.  Means +/- 2 semitones (4 total range)
      m_always_write_initial_tempo(false),
      m_always_write_initial_vol(false),
      m_always_write_initial_expression(false),
      m_always_write_initial_reverb(false),
      m_always_write_initial_pitch_bend_range(false),
      m_always_write_initial_mono_mode(false),
      m_allow_discontinuous_track_data(false),
      m_use_monophonic_tracks(false),
      m_use_linear_amplitude_scale(false),
      m_use_linear_pan_amplitude_scale(false),
      m_use_reverb(false),
      m_track_control_flow_state(false) {
  setConversionContext(ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont));
}

VGMSeq::~VGMSeq() = default;

SeqTrack* VGMSeq::sinkTrack(std::unique_ptr<SeqTrack>&& track) {
  auto* rawTrack = track.get();
  m_tracks.push_back(rawTrack);
  m_ownedTracks.emplace_back(std::move(track));
  return rawTrack;
}

void VGMSeq::clearTracks() {
  m_tracks.clear();
  m_ownedTracks.clear();
}

ISeqSlider* VGMSeq::sinkSlider(std::unique_ptr<ISeqSlider>&& slider) {
  auto* rawSlider = slider.get();
  m_sliders.emplace_back(std::move(slider));
  return rawSlider;
}

std::unique_ptr<MidiFile> VGMSeq::convertToMidi(const VGMColl* coll) {
  const auto context = ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont);
  return convertToMidi(coll, context);
}

std::unique_ptr<MidiFile> VGMSeq::convertToMidi(const VGMColl* coll, const ConversionContext& context) {
  setConversionContext(context);
  size_t numTracks = m_tracks.size();

  if (!loadTracks(READMODE_FIND_DELTA_LENGTH)) {
      return nullptr;
  }

  useColl(coll);

  // Find the greatest length of all tracks to use as stop point for every track
  long stopTime = 0;
  for (size_t i = 0; i < numTracks; i++)
    stopTime = std::max(stopTime, m_tracks[i]->totalTicks);

  auto newMidi = std::make_unique<MidiFile>(this);
  this->midi = newMidi.get();
  if (!loadTracks(READMODE_CONVERT_TO_MIDI, stopTime)) {
    this->midi = nullptr;
    m_timedEvents.clear();
    return nullptr;
  }
  this->midi = nullptr;
  return newMidi;
}

MidiTrack *VGMSeq::firstMidiTrack() {
  return m_tracks.empty() ? nullptr : m_tracks[0]->pMidiTrack;
}

bool VGMSeq::load() {
  setConversionContext(ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont));
  readMode = READMODE_ADD_TO_UI;

  if (!parseHeader())
    return false;
  if (!parseTrackPointers())
    return false;
  nNumTracks = static_cast<u32>(m_tracks.size());
  if (nNumTracks == 0)
    return false;

  return loadTracks(readMode);
}

bool VGMSeq::postLoad() {
  if (readMode == READMODE_ADD_TO_UI) {
    std::ranges::sort(aInstrumentsUsed);

    for (auto & track : m_tracks) {
      track->sortChildrenByOffset();
    }
    for (auto& track : m_ownedTracks) {
      sinkChild(std::move(track));
    }
    m_ownedTracks.clear();

    setGuessedLength();
    if (length() == 0) {
      return false;
    }
  } else if (readMode == READMODE_CONVERT_TO_MIDI) {
    midi->sort();
    m_timedEvents.finalize();
  }

  return true;
}

bool VGMSeq::loadTracks(ReadMode seqReadMode, u32 stopTime) {
  // set read mode
  this->readMode = seqReadMode;
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    m_tracks[trackNum]->readMode = seqReadMode;
  }

  if (seqReadMode == READMODE_CONVERT_TO_MIDI) {
    m_timedEvents.clear();
  }

  // reset variables
  resetVars();
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!m_tracks[trackNum]->loadTrackInit(trackNum, nullptr))
      return false;
  }

  loadTracksMain(stopTime);

  return postLoad();
}

void VGMSeq::loadTracksMain(u32 stopTime) {
  // determine the stop offsets
  std::vector<u32> aStopOffset(nNumTracks);
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (readMode == READMODE_ADD_TO_UI) {
      aStopOffset[trackNum] = endOffset();
      if (length() != 0) {
        aStopOffset[trackNum] = offset() + length();
      } else {
        if (!m_allow_discontinuous_track_data) {
          // set length from the next track by offset
          for (u32 j = 0; j < nNumTracks; j++) {
            if (m_tracks[j]->offset() > m_tracks[trackNum]->offset() &&
                m_tracks[j]->offset() < aStopOffset[trackNum]) {
              aStopOffset[trackNum] = m_tracks[j]->offset();
            }
          }
        }
      }
    } else {
      aStopOffset[trackNum] = m_tracks[trackNum]->offset() + m_tracks[trackNum]->length();
    }
  }

  // load all tracks
  if (bLoadTickByTick) {
    while (hasActiveTracks()) {
      // check time limit
      if (time >= stopTime) {
        if (readMode == READMODE_ADD_TO_UI) {
          L_WARN("{} - reached tick-by-tick stop time during load.", name());
        }

        deactivateAllTracks();
        break;
      }

      // process tracks
      for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
        if (!m_tracks[trackNum]->active)
          continue;

        // tick
        m_tracks[trackNum]->loadTrackMainLoop(aStopOffset[trackNum], stopTime);
      }

      // process sliders
      auto itrSlider = m_sliders.begin();
      while (itrSlider != m_sliders.end()) {
        auto itrNextSlider = itrSlider + 1;

        ISeqSlider *slider = itrSlider->get();
        if (slider->isStarted(time)) {
          if (slider->isActive(time)) {
            slider->write(time);
          } else {
            itrNextSlider = m_sliders.erase(itrSlider);
          }
        }

        itrSlider = itrNextSlider;
      }

      if (hasActiveTracks()) {
        onTickEnd();
      }

      if (bIncTickAfterProcessingTracks == true) {
        time++;
      }
      bIncTickAfterProcessingTracks = true;
      if (readMode == READMODE_CONVERT_TO_MIDI) {
        for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
          if (m_tracks.at(trackNum)->pMidiTrack != nullptr) {
            m_tracks[trackNum]->pMidiTrack->setDelta(time);
          }
        }
      }

      // check loop count
      const int desiredLoopRepeats = (readMode == READMODE_ADD_TO_UI) ? 0 : conversionContext().sequenceLoops;
      const int requiredPlayThroughs = desiredLoopRepeats + 1;  // include the initial playthrough
      if (foreverLoopCount() >= requiredPlayThroughs) {
        deactivateAllTracks();
        break;
      }
    }
  } else {
    u32 initialTime = time;  // preserve current time for multi section sequence

    // load track by track
    for (u32 trackNum = 0; trackNum < nNumTracks && trackNum < m_tracks.size(); trackNum++) {
      time = initialTime;

      m_tracks[trackNum]->loadTrackMainLoop(aStopOffset[trackNum], stopTime);
      m_tracks[trackNum]->active = false;
    }
  }
}

bool VGMSeq::hasActiveTracks() {
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (m_tracks[trackNum]->active)
      return true;
  }
  return false;
}

void VGMSeq::deactivateAllTracks() {
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    m_tracks[trackNum]->active = false;
  }
}

int VGMSeq::foreverLoopCount() {
  if (nNumTracks == 0)
    return 0;

  int foreverLoops = INT_MAX;
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!m_tracks[trackNum]->active)
      continue;

    if (foreverLoops > m_tracks[trackNum]->infiniteLoops)
      foreverLoops = m_tracks[trackNum]->infiniteLoops;
  }
  return (foreverLoops != INT_MAX) ? foreverLoops : 0;
}

bool VGMSeq::parseHeader() {
  return true;
}

// GetTrackPointers() should contain logic for parsing track pointers
// and instantiating/adding each track in the sequence
bool VGMSeq::parseTrackPointers() {
  return true;
}

void VGMSeq::resetVars() {
  time = 0;
  tempoBPM = initialTempoBPM;

  m_sliders.clear();

  if (readMode == READMODE_ADD_TO_UI) {
    aInstrumentsUsed.clear();
    m_referencedBanks.clear();
  }
}

void VGMSeq::setPPQN(u16 ppqn) {
  this->m_ppqn = ppqn;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    midi->setPPQN(ppqn);
}

u16 VGMSeq::ppqn() const {
  return this->m_ppqn;
  //return midi->GetPPQN();
}

void VGMSeq::addInstrumentRef(u32 progNum) {
  if (std::ranges::find(aInstrumentsUsed, progNum) == aInstrumentsUsed.end()) {
    aInstrumentsUsed.push_back(progNum);
  }
}

void VGMSeq::addBankReference(u16 bank) {
  m_referencedBanks.insert(bank);
}

const std::set<u16>& VGMSeq::referencedBanks() const {
  return m_referencedBanks;
}

bool VGMSeq::saveAsMidi(const std::filesystem::path &filepath, const VGMColl* coll) {
  const auto context = ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont);
  return saveAsMidi(filepath, coll, context);
}

bool VGMSeq::saveAsMidi(const std::filesystem::path& filepath,
                        const VGMColl* coll,
                        const ConversionContext& context) {
  auto midiFile = this->convertToMidi(coll, context);
  if (!midiFile)
    return false;
  bool result = midiFile->saveMidiFile(filepath);
  return result;
}

/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <climits>
#include <ranges>

#include "VGMSeq.h"
#include "SeqEvent.h"
#include "SeqSlider.h"
#include "Options.h"
#include "Root.h"
#include "Format.h"
#include "helper.h"

VGMSeq::VGMSeq(const std::string &format, RawFile *file, uint32_t offset, uint32_t length, std::string name)
    : VGMFile(format, file, offset, length, std::move(name)),
      midi(nullptr),
      nNumTracks(0),
      readMode(READMODE_ADD_TO_UI),
      time(0),
      m_use_monophonic_tracks(false),
      m_use_linear_amplitude_scale(false),
      m_use_linear_pan_amplitude_scale(false),
      m_always_write_initial_tempo(false),
      m_always_write_initial_vol(false),
      m_always_write_initial_expression(false),
      m_always_write_initial_reverb(false),
      m_always_write_initial_pitch_bend_range(false),
      m_always_write_initial_mono_mode(false),
      m_allow_discontinuous_track_data(false),
      bLoadTickByTick(false),
      bIncTickAfterProcessingTracks(true),
      m_initial_volume(100),                    // GM standard (dls1 spec p16)
      m_initial_expression(127),             //''
      m_initial_reverb_level(40),                  // GM standard
      m_initial_pitch_bend_range_cents(200), // GM standard.  Means +/- 2 semitones (4 total range)
      initialTempoBPM(120),
      m_use_reverb(false) {
}

VGMSeq::~VGMSeq() {
  deleteVect<ISeqSlider>(aSliders);
  delete midi;
}

bool VGMSeq::loadVGMFile() {
  bool val = load();
  if (!val) {
    return false;
  }

  if (auto fmt = format(); fmt) {
    fmt->onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
  }

  return val;
}

bool VGMSeq::load() {
  if (!loadMain())
    return false;

  rawFile()->addContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *,
    VGMMiscFile *>>(this));
  pRoot->addVGMFile(this);
  return true;
}

MidiFile *VGMSeq::convertToMidi() {
  size_t numTracks = aTracks.size();

  if (m_hasConditionalBranches) {
    buildBranchTimeline();
    bLoadTickByTick = true;     // mandatory for perfect sync
  }

  if (!loadTracks(READMODE_FIND_DELTA_LENGTH)) {
      return nullptr;
  }

  // Find the greatest length of all tracks to use as stop point for every track
  long stopTime = 0;
  for (size_t i = 0; i < numTracks; i++)
    stopTime = std::max(stopTime, aTracks[i]->totalTicks);

  auto *newmidi = new MidiFile(this);
  this->midi = newmidi;
  if (!loadTracks(READMODE_CONVERT_TO_MIDI, stopTime)) {
    delete midi;
    this->midi = nullptr;
    return nullptr;
  }
  this->midi = nullptr;
  return newmidi;
}

MidiTrack *VGMSeq::firstMidiTrack() {
  return aTracks.empty() ? nullptr : aTracks[0]->pMidiTrack;
}

// Load() - Function to load all the sequence data into the class
bool VGMSeq::loadMain() {
  readMode = READMODE_ADD_TO_UI;

  if (!parseHeader())
    return false;
  if (!parseTrackPointers())
    return false;
  nNumTracks = static_cast<uint32_t>(aTracks.size());
  if (nNumTracks == 0)
    return false;

  return loadTracks(readMode);
}

bool VGMSeq::postLoad() {
  if (readMode == READMODE_ADD_TO_UI) {
    std::ranges::sort(aInstrumentsUsed);

    for (auto & track : aTracks) {
      track->sortChildrenByOffset();
    }
    addChildren(aTracks);

    setGuessedLength();
    if (unLength == 0) {
      return false;
    }
  } else if (readMode == READMODE_CONVERT_TO_MIDI) {
    midi->sort();
  }

  return true;
}

bool VGMSeq::loadTracks(ReadMode readMode, uint32_t stopTime) {
  // set read mode
  this->readMode = readMode;
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    aTracks[trackNum]->readMode = readMode;
  }

  // reset variables
  resetVars();
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!aTracks[trackNum]->loadTrackInit(trackNum, nullptr))
      return false;
  }

  loadTracksMain(stopTime);

  return postLoad();
}

void VGMSeq::loadTracksMain(uint32_t stopTime) {
  // determine the stop offsets
  if (readMode != READMODE_ADD_TO_UI)
    m_nextTimelineIdx = 0;

  uint32_t *aStopOffset = new uint32_t[nNumTracks];
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (readMode == READMODE_ADD_TO_UI) {
      aStopOffset[trackNum] = endOffset();
      if (unLength != 0) {
        aStopOffset[trackNum] = dwOffset + unLength;
      } else {
        if (!m_allow_discontinuous_track_data) {
          // set length from the next track by offset
          for (uint32_t j = 0; j < nNumTracks; j++) {
            if (aTracks[j]->dwOffset > aTracks[trackNum]->dwOffset &&
                aTracks[j]->dwOffset < aStopOffset[trackNum]) {
              aStopOffset[trackNum] = aTracks[j]->dwOffset;
            }
          }
        }
      }
    } else {
      aStopOffset[trackNum] = aTracks[trackNum]->dwOffset + aTracks[trackNum]->unLength;
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

          suspendAllActiveTracks();
          break;
        }

      //   update condition value  (NEW)
      printf("Time: %d   m_timeline[m_nextTimelineIdx].absTime: %d\n", time, m_timeline[m_nextTimelineIdx].absTime);
      while (m_nextTimelineIdx < m_timeline.size() &&
             time >= m_timeline[m_nextTimelineIdx].absTime) {
        m_curCondValue = m_timeline[m_nextTimelineIdx].newValue;
        ++m_nextTimelineIdx;
      }

      // process tracks

      for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
        if (aTracks[trackNum]->state() != SeqTrack::State::Active)
          continue;

        // tick
        aTracks[trackNum]->loadTrackMainLoop(aStopOffset[trackNum], stopTime);
      }

      // process sliders
      auto itrSlider = aSliders.begin();
      while (itrSlider != aSliders.end()) {
        auto itrNextSlider = itrSlider + 1;

        ISeqSlider *slider = *itrSlider;
        if (slider->isStarted(time)) {
          if (slider->isActive(time)) {
            slider->write(time);
          } else {
            itrNextSlider = aSliders.erase(itrSlider);
          }
        }

        itrSlider = itrNextSlider;
      }

      if (bIncTickAfterProcessingTracks == true) {
        time++;
      }
      bIncTickAfterProcessingTracks = true;
      if (readMode == READMODE_CONVERT_TO_MIDI) {
        for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
          if (aTracks.at(trackNum)->pMidiTrack != nullptr) {
            aTracks[trackNum]->pMidiTrack->setDelta(time);
          }
        }
      }

      // check loop count
      int requiredLoops = (readMode == READMODE_ADD_TO_UI) ? 1 : ConversionOptions::the().numSequenceLoops();
      if (foreverLoopCount() > requiredLoops) {
        suspendAllActiveTracks();
        break;
      }
    }
  } else {
    uint32_t initialTime = time;  // preserve current time for multi section sequence

    // load track by track
    for (uint32_t trackNum = 0; trackNum < nNumTracks && trackNum < aTracks.size(); trackNum++) {
      time = initialTime;

      aTracks[trackNum]->loadTrackMainLoop(aStopOffset[trackNum], stopTime);
      aTracks[trackNum]->setState(SeqTrack::State::Finished);
    }
  }
  delete[] aStopOffset;
}

bool VGMSeq::hasActiveTracks() {
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (aTracks[trackNum]->state() == SeqTrack::State::Active)
      return true;
  }
  return false;
}

void VGMSeq::deactivateAllTracks() {
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    aTracks[trackNum]->setState(SeqTrack::State::Finished);
  }
}

void VGMSeq::suspendAllActiveTracks() {
  for (const auto track : aTracks) {
    if (track->state() == SeqTrack::State::Active) {
      track->setState(SeqTrack::State::Suspended);
    }
  }
}

void VGMSeq::activateAllSuspendedTracks() {
  for (const auto track : aTracks) {
    if (track->state() == SeqTrack::State::Suspended) {
      track->setState(SeqTrack::State::Active);
    }
  }
}

int VGMSeq::foreverLoopCount() {
  if (nNumTracks == 0)
    return 0;

  int foreverLoops = INT_MAX;
  for (const auto track : aTracks) {
    if (track->state() != SeqTrack::State::Active)
      continue;

    if (foreverLoops > track->foreverLoops)
      foreverLoops = track->foreverLoops;
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

  deleteVect<ISeqSlider>(aSliders);

  if (readMode == READMODE_ADD_TO_UI) {
    aInstrumentsUsed.clear();

    // -------- NEW --------
    m_branchesFound.clear();
    m_timeline.clear();
    m_hasConditionalBranches = false;
    // ---------------------
  }
}

void VGMSeq::setPPQN(uint16_t ppqn) {
  this->m_ppqn = ppqn;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    midi->setPPQN(ppqn);
}

uint16_t VGMSeq::ppqn() const {
  return this->m_ppqn;
  //return midi->GetPPQN();
}

void VGMSeq::addInstrumentRef(uint32_t progNum) {
  if (std::ranges::find(aInstrumentsUsed, progNum) == aInstrumentsUsed.end()) {
    aInstrumentsUsed.push_back(progNum);
  }
}

bool VGMSeq::saveAsMidi(const std::string &filepath) {
  MidiFile *midi = this->convertToMidi();
  if (!midi)
    return false;
  bool result = midi->saveMidiFile(filepath);
  delete midi;
  return result;
}


void VGMSeq::buildBranchTimeline()
{
  //----------------------------------------------------------------
  // 1.  Keep the LAST encounter of every opcode                  --
  //----------------------------------------------------------------
  std::unordered_map<uint32_t /*src offset*/, CondBranchEvt> last;
  for (const auto& b : m_branchesFound)
    last[b.srcOffset] = b;               // later occurrences overwrite

  std::vector<CondBranchEvt> uniq;
  uniq.reserve(last.size());
  for (auto& kv : last) uniq.push_back(kv.second);

  std::sort(uniq.begin(), uniq.end(),
            [](auto& a, auto& b) { return a.absTime < b.absTime; });

  //----------------------------------------------------------------
  // 2.  Build a linear time-line: every time the expected value   --
  //     differs from the current one, schedule a change one tick --
  //     *after* the last encounter.                              --
  //----------------------------------------------------------------
  m_timeline.clear();
  uint8_t curVal = m_curCondValue;
  for (const auto& b : uniq) {
    if (b.expectValue != curVal) {
      // m_timeline.push_back({ b.absTime + 1, b.expectValue });
      m_timeline.push_back({ b.absTime, b.expectValue });
      curVal = b.expectValue;
    }
  }
  m_nextTimelineIdx = 0;
}
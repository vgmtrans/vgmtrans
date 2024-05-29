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
      bMonophonicTracks(false),
      bUseLinearAmplitudeScale(false),
      bUseLinearPanAmplitudeScale(false),
      bAlwaysWriteInitialTempo(false),
      bAlwaysWriteInitialVol(false),
      bAlwaysWriteInitialExpression(false),
      bAlwaysWriteInitialReverb(false),
      bAlwaysWriteInitialPitchBendRange(false),
      bAlwaysWriteInitialMono(false),
      bAllowDiscontinuousTrackData(false),
      bLoadTickByTick(false),
      bIncTickAfterProcessingTracks(true),
      initialVol(100),                    // GM standard (dls1 spec p16)
      initialExpression(127),             //''
      initialReverb(40),                  // GM standard
      initialPitchBendRangeSemiTones(2),  // GM standard.  Means +/- 2 semitones (4 total range)
      initialPitchBendRangeCents(0),
      initialTempoBPM(120),
      bReverb(false) {
  AddContainer<SeqTrack>(aTracks);
}

VGMSeq::~VGMSeq() {
  DeleteVect<SeqTrack>(aTracks);
  DeleteVect<ISeqSlider>(aSliders);
  delete midi;
}

bool VGMSeq::LoadVGMFile() {
  bool val = Load();
  if (!val) {
    return false;
  }

  if (auto fmt = GetFormat(); fmt) {
    fmt->OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
  }

  return val;
}

bool VGMSeq::Load() {
  if (!LoadMain())
    return false;

  rawfile->AddContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *,
    VGMMiscFile *>>(this));
  pRoot->AddVGMFile(this);
  return true;
}

MidiFile *VGMSeq::ConvertToMidi() {
  size_t numTracks = aTracks.size();

  if (!LoadTracks(READMODE_FIND_DELTA_LENGTH)) {
      return nullptr;
  }

  // Find the greatest length of all tracks to use as stop point for every track
  long stopTime = 0;
  for (size_t i = 0; i < numTracks; i++)
    stopTime = std::max(stopTime, aTracks[i]->totalTicks);

  auto *newmidi = new MidiFile(this);
  this->midi = newmidi;
  if (!LoadTracks(READMODE_CONVERT_TO_MIDI, stopTime)) {
    delete midi;
    this->midi = nullptr;
    return nullptr;
  }
  this->midi = nullptr;
  return newmidi;
}

MidiTrack *VGMSeq::GetFirstMidiTrack() {
  MidiTrack *pFirstMidiTrack = nullptr;
  if (!aTracks.empty()) {
    pFirstMidiTrack = aTracks[0]->pMidiTrack;
  }
  return pFirstMidiTrack;
}

// Load() - Function to load all the sequence data into the class
bool VGMSeq::LoadMain() {
  readMode = READMODE_ADD_TO_UI;

  if (!GetHeaderInfo())
    return false;
  if (!GetTrackPointers())
    return false;
  nNumTracks = static_cast<uint32_t>(aTracks.size());
  if (nNumTracks == 0)
    return false;

  return LoadTracks(readMode);
}

bool VGMSeq::PostLoad() {
  if (readMode == READMODE_ADD_TO_UI) {
    std::ranges::sort(aInstrumentsUsed);

    for (auto & aTrack : aTracks) {
      std::ranges::sort(aTrack->aEvents, [](const VGMItem *a, const VGMItem *b) {
        return a->dwOffset < b->dwOffset;
      });
    }
  } else if (readMode == READMODE_CONVERT_TO_MIDI) {
    midi->Sort();
  }

  return true;
}

bool VGMSeq::LoadTracks(ReadMode readMode, uint32_t stopTime) {
  bool succeeded = true;

  // set read mode
  this->readMode = readMode;
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    aTracks[trackNum]->readMode = readMode;
  }

  // reset variables
  ResetVars();
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!aTracks[trackNum]->LoadTrackInit(trackNum, nullptr))
      return false;
  }

  LoadTracksMain(stopTime);
  if (readMode == READMODE_ADD_TO_UI) {
    SetGuessedLength();
    if (unLength == 0) {
      return false;
    }
  }

  if (!PostLoad()) {
    succeeded = false;
  }

  return succeeded;
}

void VGMSeq::LoadTracksMain(uint32_t stopTime) {
  // determine the stop offsets
  uint32_t *aStopOffset = new uint32_t[nNumTracks];
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (readMode == READMODE_ADD_TO_UI) {
      aStopOffset[trackNum] = GetEndOffset();
      if (unLength != 0) {
        aStopOffset[trackNum] = dwOffset + unLength;
      } else {
        if (!bAllowDiscontinuousTrackData) {
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
    while (HasActiveTracks()) {
      // check time limit
      if (time >= stopTime) {
        if (readMode == READMODE_ADD_TO_UI) {
          L_WARN("{} - reached tick-by-tick stop time during load.", name());
        }

        InactivateAllTracks();
        break;
      }

      // process tracks
      for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
        if (!aTracks[trackNum]->active)
          continue;

        // tick
        aTracks[trackNum]->LoadTrackMainLoop(aStopOffset[trackNum], stopTime);
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
            aTracks[trackNum]->pMidiTrack->SetDelta(time);
          }
        }
      }

      // check loop count
      int requiredLoops = (readMode == READMODE_ADD_TO_UI) ? 1 : ConversionOptions::the().GetNumSequenceLoops();
      if (GetForeverLoops() >= requiredLoops) {
        InactivateAllTracks();
        break;
      }
    }
  } else {
    uint32_t initialTime = time;  // preserve current time for multi section sequence

    // load track by track
    for (uint32_t trackNum = 0; trackNum < nNumTracks && trackNum < aTracks.size(); trackNum++) {
      time = initialTime;

      aTracks[trackNum]->LoadTrackMainLoop(aStopOffset[trackNum], stopTime);
      aTracks[trackNum]->active = false;
    }
  }
  delete[] aStopOffset;
}

bool VGMSeq::HasActiveTracks() {
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (aTracks[trackNum]->active)
      return true;
  }
  return false;
}

void VGMSeq::InactivateAllTracks() {
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    aTracks[trackNum]->active = false;
  }
}

int VGMSeq::GetForeverLoops() {
  if (nNumTracks == 0)
    return 0;

  int foreverLoops = INT_MAX;
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!aTracks[trackNum]->active)
      continue;

    if (foreverLoops > aTracks[trackNum]->foreverLoops)
      foreverLoops = aTracks[trackNum]->foreverLoops;
  }
  return (foreverLoops != INT_MAX) ? foreverLoops : 0;
}

bool VGMSeq::GetHeaderInfo() {
  return true;
}

// GetTrackPointers() should contain logic for parsing track pointers
// and instantiating/adding each track in the sequence
bool VGMSeq::GetTrackPointers() {
  return true;
}

void VGMSeq::ResetVars() {
  time = 0;
  tempoBPM = initialTempoBPM;

  DeleteVect<ISeqSlider>(aSliders);

  if (readMode == READMODE_ADD_TO_UI) {
    aInstrumentsUsed.clear();
  }
}

void VGMSeq::SetPPQN(uint16_t ppqn) {
  this->ppqn = ppqn;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    midi->SetPPQN(ppqn);
}

uint16_t VGMSeq::GetPPQN() const {
  return this->ppqn;
  //return midi->GetPPQN();
}

void VGMSeq::AddInstrumentRef(uint32_t progNum) {
  if (std::ranges::find(aInstrumentsUsed, progNum) == aInstrumentsUsed.end()) {
    aInstrumentsUsed.push_back(progNum);
  }
}

bool VGMSeq::SaveAsMidi(const std::string &filepath) {
  MidiFile *midi = this->ConvertToMidi();
  if (!midi)
    return false;
  bool result = midi->SaveMidiFile(filepath);
  delete midi;
  return result;
}

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SequencePlayer.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

#include <QMetaObject>
#include <QTimer>

namespace {

struct MemoryFile {
  QWORD position = 0;
  std::vector<u8> bytes;

  static DWORD read(void* destination, DWORD count, void* handle) {
    auto* file = static_cast<MemoryFile*>(handle);
    if (destination == nullptr || file == nullptr || file->position >= file->bytes.size()) {
      return 0;
    }
    const auto begin = static_cast<size_t>(file->position);
    const auto size = std::min<size_t>(count, file->bytes.size() - begin);
    std::copy_n(file->bytes.data() + begin, size, static_cast<u8*>(destination));
    file->position += size;
    return static_cast<DWORD>(size);
  }

  static BOOL seek(QWORD offset, void* handle) {
    auto* file = static_cast<MemoryFile*>(handle);
    if (file == nullptr || offset > file->bytes.size()) {
      return false;
    }
    file->position = offset;
    return true;
  }

  static QWORD length(void* handle) {
    const auto* file = static_cast<const MemoryFile*>(handle);
    return file != nullptr ? file->bytes.size() : 0;
  }

  static void close(void* handle) { delete static_cast<MemoryFile*>(handle); }
};

constexpr BASS_FILEPROCS kMemoryFileCallbacks{
    MemoryFile::close,
    MemoryFile::length,
    MemoryFile::read,
    MemoryFile::seek,
};
constexpr int kPositionPollIntervalMs = 1000 / 60;

int tickValue(QWORD value) {
  if (value == static_cast<QWORD>(-1)) {
    return 0;
  }
  return static_cast<int>(std::min<QWORD>(value, std::numeric_limits<int>::max()));
}

}  // namespace

SequencePlayer::SequencePlayer(QObject* parent) : QObject(parent), positionTimer_(new QTimer(this)) {
  audioReady_ = BASS_Init(-1, 44100, 0, nullptr, nullptr) != 0;
  positionTimer_->setInterval(kPositionPollIntervalMs);
  connect(positionTimer_, &QTimer::timeout, this, [this] {
    if (!activeStream_) {
      return;
    }
    const DWORD state = BASS_ChannelIsActive(activeStream_);
    if (state == BASS_ACTIVE_PLAYING) {
      emit positionChanged(elapsedTicks(), totalTicks(), PositionChangeOrigin::Playback);
    } else if (state == BASS_ACTIVE_STOPPED) {
      stop();
    }
  });
}

SequencePlayer::~SequencePlayer() {
  stop();
  if (audioReady_) {
    BASS_Free();
  }
}

bool SequencePlayer::load(vgmtrans::core::CollectionPlayback playback) {
  if (!playback.playable()) {
    emit errorOccurred(tr("The collection did not produce playable MIDI and SoundFont data."));
    return false;
  }
  if (!audioReady_) {
    emit errorOccurred(bassError(tr("Could not initialize the audio device")));
    return false;
  }

  auto soundFontFile = std::make_unique<MemoryFile>(MemoryFile{
      .bytes = std::move(playback.soundFont),
  });
  const HSOUNDFONT soundFont =
      BASS_MIDI_FontInitUser(&kMemoryFileCallbacks, soundFontFile.get(), BASS_MIDI_FONT_XGDRUMS);
  if (!soundFont) {
    emit errorOccurred(bassError(tr("Could not load the generated SoundFont")));
    return false;
  }
  soundFontFile.release();

  const HSTREAM stream =
      BASS_MIDI_StreamCreateFile(true, playback.midi.data(), 0, playback.midi.size(), BASS_MIDI_DECAYEND, 0);
  if (!stream) {
    const QString error = bassError(tr("Could not load the generated MIDI"));
    BASS_MIDI_FontFree(soundFont);
    emit errorOccurred(error);
    return false;
  }

  BASS_MIDI_FONT font{
      .font = soundFont,
      .preset = -1,
      .bank = 0,
  };
  if (!BASS_MIDI_StreamSetFonts(stream, &font, 1)) {
    const QString error = bassError(tr("Could not assign the generated SoundFont"));
    BASS_StreamFree(stream);
    BASS_MIDI_FontFree(soundFont);
    emit errorOccurred(error);
    return false;
  }

  static_cast<void>(BASS_ChannelSetAttribute(stream, BASS_ATTRIB_MIDI_CHANS, 128));
  static_cast<void>(BASS_ChannelFlags(stream, 0, BASS_MIDI_NOFX));
  static_cast<void>(BASS_ChannelSetSync(stream, BASS_SYNC_END, 0, &SequencePlayer::playbackEnded, this));

  stop();
  activeStream_ = stream;
  loadedSoundFont_ = soundFont;
  activePlayback_ = std::move(playback);

  if (!BASS_ChannelPlay(activeStream_, false)) {
    const QString error = bassError(tr("Could not start playback"));
    stop();
    emit errorOccurred(error);
    return false;
  }
  positionTimer_->start();
  emit positionChanged(0, totalTicks(), PositionChangeOrigin::Playback);
  emit stateChanged(true, true);
  return true;
}

void SequencePlayer::toggle() {
  if (!activeStream_) {
    return;
  }
  if (playing()) {
    if (!BASS_ChannelPause(activeStream_)) {
      emit errorOccurred(bassError(tr("Could not pause playback")));
      return;
    }
    positionTimer_->stop();
  } else if (BASS_ChannelPlay(activeStream_, false)) {
    positionTimer_->start();
  } else {
    emit errorOccurred(bassError(tr("Could not resume playback")));
  }
  emit stateChanged(playing(), true);
}

void SequencePlayer::stop() {
  positionTimer_->stop();
  if (activeStream_) {
    BASS_ChannelStop(activeStream_);
    BASS_StreamFree(activeStream_);
    activeStream_ = 0;
  }
  if (loadedSoundFont_) {
    BASS_MIDI_FontFree(loadedSoundFont_);
    loadedSoundFont_ = 0;
  }
  activePlayback_.reset();
  emit positionChanged(0, 1, PositionChangeOrigin::Playback);
  emit stateChanged(false, false);
}

void SequencePlayer::seek(int position, PositionChangeOrigin origin) {
  if (!activeStream_) {
    return;
  }
  const int maximum = totalTicks();
  const int target = std::clamp(position, 0, maximum);
  if (!BASS_ChannelSetPosition(activeStream_, static_cast<QWORD>(target), BASS_POS_MIDI_TICK)) {
    emit errorOccurred(bassError(tr("Could not seek playback")));
    return;
  }
  emit positionChanged(target, maximum, origin);
}

bool SequencePlayer::playing() const {
  return activeStream_ && BASS_ChannelIsActive(activeStream_) == BASS_ACTIVE_PLAYING;
}

vgmtrans::core::CollectionId SequencePlayer::activeCollection() const noexcept {
  return activePlayback_ ? activePlayback_->collection : vgmtrans::core::CollectionId{};
}

vgmtrans::core::AssetId SequencePlayer::activeSequence() const noexcept {
  return activePlayback_ ? activePlayback_->sequence : vgmtrans::core::AssetId{};
}

std::span<const vgmtrans::core::AssetId> SequencePlayer::activeAssets() const noexcept {
  return activePlayback_ ? std::span<const vgmtrans::core::AssetId>(activePlayback_->assetDependencies)
                         : std::span<const vgmtrans::core::AssetId>{};
}

const vgmtrans::core::PerformanceSequence* SequencePlayer::activePerformance() const noexcept {
  return activePlayback_ ? &activePlayback_->performance : nullptr;
}

int SequencePlayer::elapsedTicks() const {
  return activeStream_ ? tickValue(BASS_ChannelGetPosition(activeStream_, BASS_POS_MIDI_TICK)) : 0;
}

int SequencePlayer::totalTicks() const {
  return activeStream_ ? std::max(1, tickValue(BASS_ChannelGetLength(activeStream_, BASS_POS_MIDI_TICK))) : 1;
}

QString SequencePlayer::bassError(const QString& action) const {
  return tr("%1 (BASS error %2).").arg(action).arg(BASS_ErrorGetCode());
}

void CALLBACK SequencePlayer::playbackEnded(HSYNC, DWORD channel, DWORD, void* user) {
  auto* player = static_cast<SequencePlayer*>(user);
  if (player == nullptr) {
    return;
  }
  QMetaObject::invokeMethod(
      player, [player, stream = static_cast<HSTREAM>(channel)] { player->handlePlaybackEnded(stream); },
      Qt::QueuedConnection);
}

void SequencePlayer::handlePlaybackEnded(HSTREAM stream) {
  if (stream == activeStream_) {
    stop();
  }
}

/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PlaybackPosition.h"
#include "value/export/Export.h"

#include <bass.h>
#include <bassmidi.h>

#include <optional>
#include <span>

#include <QObject>

class QTimer;

// Owns the audio device and the one prepared value collection currently loaded
// into BASSMIDI. Collection rendering remains in the value core.
class SequencePlayer final : public QObject {
  Q_OBJECT

public:
  explicit SequencePlayer(QObject* parent = nullptr);
  ~SequencePlayer() override;

  SequencePlayer(const SequencePlayer&) = delete;
  SequencePlayer& operator=(const SequencePlayer&) = delete;

  [[nodiscard]] bool load(vgmtrans::core::CollectionPlayback playback);
  void toggle();
  void stop();
  void seek(int position, PositionChangeOrigin origin);

  [[nodiscard]] bool playing() const;
  [[nodiscard]] bool hasActiveCollection() const noexcept { return activePlayback_.has_value(); }
  [[nodiscard]] vgmtrans::core::CollectionId activeCollection() const noexcept;
  [[nodiscard]] vgmtrans::core::AssetId activeSequence() const noexcept;
  [[nodiscard]] std::span<const vgmtrans::core::AssetId> activeAssets() const noexcept;
  [[nodiscard]] std::span<const vgmtrans::core::SourcePlaybackSpan> activeSourceSpans() const noexcept;

signals:
  void stateChanged(bool playing, bool hasActiveCollection);
  void positionChanged(int current, int maximum, PositionChangeOrigin origin);
  void errorOccurred(const QString& message);

private:
  [[nodiscard]] int elapsedTicks() const;
  [[nodiscard]] int totalTicks() const;
  [[nodiscard]] QString bassError(const QString& action) const;

  bool audioReady_ = false;
  HSTREAM activeStream_{};
  HSOUNDFONT loadedSoundFont_{};
  std::optional<vgmtrans::core::CollectionPlayback> activePlayback_;
  QTimer* positionTimer_{};
};

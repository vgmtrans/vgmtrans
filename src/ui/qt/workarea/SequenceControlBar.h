/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <QColor>
#include <QWidget>

class QDoubleSpinBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;

class SequenceControlBar final : public QWidget {
  Q_OBJECT

public:
  struct StripConfig {
    int id = -1;
    QString title;
    QString subtitle;
    QColor borderColor;
    int midiChannel = -1;
    int pan = 64;
    int volume = 100;
  };

  explicit SequenceControlBar(QWidget* parent = nullptr);
  ~SequenceControlBar() override;

  void setTempoBpm(double bpm);
  [[nodiscard]] double tempoBpm() const;

  void setStrips(const std::vector<StripConfig>& strips);
  [[nodiscard]] std::vector<int> stripIds() const;

  void setStripPan(int stripId, int pan);
  void setStripVolume(int stripId, int volume);
  void setStripMuted(int stripId, bool muted);
  void setStripSolo(int stripId, bool solo);

  [[nodiscard]] bool stripMuted(int stripId) const;
  [[nodiscard]] bool stripSolo(int stripId) const;

signals:
  void tempoChanged(double bpm);
  void stripMuteChanged(int stripId, bool muted);
  void stripSoloChanged(int stripId, bool solo);
  void stripPanChanged(int stripId, int pan);
  void stripVolumeChanged(int stripId, int volume);

protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct StripWidgets;

  StripWidgets* findStrip(int stripId);
  const StripWidgets* findStrip(int stripId) const;

  void rebuildStrips(const std::vector<StripConfig>& strips);
  void refreshStripInteractivity();
  void refreshScrollControls();
  void scrollBlocks(int deltaPixels);
  void refreshStyleSheet();
  void applyStripFrameStyle(StripWidgets& strip, bool dimmed, bool soloed);

  bool m_updatingUi = false;
  QDoubleSpinBox* m_tempoSpin = nullptr;
  QLineEdit* m_tempoLineEdit = nullptr;
  QScrollArea* m_stripScroll = nullptr;
  QWidget* m_stripContainer = nullptr;
  QHBoxLayout* m_stripLayout = nullptr;
  QToolButton* m_scrollLeft = nullptr;
  QToolButton* m_scrollRight = nullptr;
  QWidget* m_scrollControls = nullptr;

  std::vector<std::unique_ptr<StripWidgets>> m_strips;
};

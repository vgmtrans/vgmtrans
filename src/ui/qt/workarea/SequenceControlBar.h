/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QColor>
#include <QWidget>

class QDoubleSpinBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPaintEvent;
class QScrollArea;
class QToolButton;

class SequenceControlBar final : public QWidget {
  Q_OBJECT

public:
  struct ChannelConfig {
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

  void setChannels(const std::vector<ChannelConfig>& channels);
  [[nodiscard]] std::vector<int> channelIds() const;

  void setChannelPan(int channelId, int pan);
  void setChannelVolume(int channelId, int volume);
  void setChannelMuted(int channelId, bool muted);
  void setChannelSolo(int channelId, bool solo);

  [[nodiscard]] bool channelMuted(int channelId) const;
  [[nodiscard]] bool channelSolo(int channelId) const;

signals:
  void tempoChanged(double bpm);
  void chanMuteChanged(int channelId, bool muted);
  void chanSoloChanged(int channelId, bool solo);
  void chanPanChanged(int channelId, int pan);
  void chanVolumeChanged(int channelId, int volume);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct BlockWidgets;

  BlockWidgets* findBlock(int channelId);
  const BlockWidgets* findBlock(int channelId) const;

  void rebuildChannelBlocks(const std::vector<ChannelConfig>& channels);
  void refreshBlockInteractivity();
  void refreshScrollControls();
  void scrollBlocks(int deltaPixels);
  void refreshStyleSheet();
  void applyBlockFrameStyle(BlockWidgets& block, bool dimmed, bool soloed);

  bool m_updatingUi = false;
  QDoubleSpinBox* m_tempoSpin = nullptr;
  QLineEdit* m_tempoLineEdit = nullptr;
  QScrollArea* m_blockScroll = nullptr;
  QWidget* m_blockContainer = nullptr;
  QHBoxLayout* m_blockLayout = nullptr;
  QToolButton* m_scrollLeft = nullptr;
  QToolButton* m_scrollRight = nullptr;
  QWidget* m_scrollControls = nullptr;

  std::vector<std::unique_ptr<BlockWidgets>> m_blocks;
  std::unordered_map<int, BlockWidgets*> m_blocksById;
};

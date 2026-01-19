/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class QParallelAnimationGroup;
class QWidget;
class VGMFile;
class VGMItem;
class HexViewRhiWindow;

class HexView final : public QAbstractScrollArea {
  Q_OBJECT
  Q_PROPERTY(qreal overlayOpacity READ overlayOpacity WRITE setOverlayOpacity)
  Q_PROPERTY(qreal shadowBlur READ shadowBlur WRITE setShadowBlur)
  Q_PROPERTY(QPointF shadowOffset READ shadowOffset WRITE setShadowOffset)
  Q_PROPERTY(qreal shadowStrength READ shadowStrength WRITE setShadowStrength)

public:
  explicit HexView(VGMFile* vgmfile, QWidget* parent = nullptr);
  ~HexView() override;
  void setSelectedItem(VGMItem* item);
  void setFont(const QFont& font);
  [[nodiscard]] int getVirtualFullWidth() const;
  [[nodiscard]] int getVirtualWidthSansAscii() const;
  [[nodiscard]] int getVirtualWidthSansAsciiAndAddress() const;
  [[nodiscard]] int getActualVirtualWidth() const;
  [[nodiscard]] int getViewportFullWidth() const;
  [[nodiscard]] int getViewportWidthSansAscii() const;
  [[nodiscard]] int getViewportWidthSansAsciiAndAddress() const;

  // In HexView public API (you add this):
  void handleCoalescedMouseMove(const QPoint& pos,
                                Qt::MouseButtons buttons,
                                Qt::KeyboardModifiers mods);

signals:
  void selectionChanged(VGMItem* item);

protected:
  bool viewportEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void changeEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  friend class HexViewRhiWindow;
  struct SelectionRange {
    uint32_t offset;
    uint32_t length;
  };
  struct Style {
    QColor bg;
    QColor fg;
  };
  struct GlyphAtlas {
    QImage image;
    std::array<QRectF, 128> uvTable{};
    qreal dpr = 0.0;
    int glyphWidth = 0;
    int glyphHeight = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    uint64_t version = 0;
    QFont font;
  };

  int hexXOffset() const;
  int getVirtualHeight() const;
  int getTotalLines() const;
  int getOffsetFromPoint(QPoint pos) const;
  void updateLayout();
  void updateScrollBars();
  void rebuildStyleMap();
  void ensureGlyphAtlas(qreal dpr);
  qreal overlayOpacity() const;
  void setOverlayOpacity(qreal opacity);
  qreal shadowBlur() const;
  void setShadowBlur(qreal blur);
  QPointF shadowOffset() const;
  void setShadowOffset(const QPointF& offset);
  qreal shadowStrength() const;
  void setShadowStrength(qreal s);
  void initAnimations();
  void showSelectedItem(bool show, bool animate);
  void clearFadeSelection();

  VGMFile* m_vgmfile = nullptr;
  VGMItem* m_selectedItem = nullptr;
  uint32_t m_selectedOffset = 0;
  int m_charWidth = 0;
  int m_charHalfWidth = 0;
  int m_lineHeight = 0;
  bool m_addressAsHex = true;
  bool m_isDragging = false;
  bool m_shouldDrawOffset = true;
  bool m_shouldDrawAscii = true;

  mutable int m_virtual_full_width = -1;
  mutable int m_virtual_width_sans_ascii = -1;
  mutable int m_virtual_width_sans_ascii_and_address = -1;

  std::vector<Style> m_styles;
  std::vector<uint16_t> m_styleIds;
  std::unordered_map<int, uint16_t> m_typeToStyleId;

  std::vector<SelectionRange> m_selections;
  std::vector<SelectionRange> m_fadeSelections;

  QParallelAnimationGroup* m_selectionAnimation = nullptr;
  qreal m_overlayOpacity = 0.0;
  qreal m_shadowBlur = 0.0;
  QPointF m_shadowOffset{0.0, 0.0};
  qreal m_shadowStrength = 1.0;

  HexViewRhiWindow* m_rhiWindow = nullptr;
  std::unique_ptr<GlyphAtlas> m_glyphAtlas;
};

/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWidget>
#include <QCache>

class QParallelAnimationGroup;
class QGraphicsDropShadowEffect;
class VGMFile;
class VGMItem;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

class HexView : public QWidget {
  Q_OBJECT
public:
  explicit HexView(VGMFile* vgmfile, QWidget *parent = nullptr);
  void setSelectedItem(VGMItem* item);
  void setFont(QFont& font);
  [[nodiscard]] int getVirtualFullWidth();
  [[nodiscard]] int getVirtualWidthSansAscii();
  [[nodiscard]] int getVirtualWidthSansAsciiAndAddress();
  [[nodiscard]] int getActualVirtualWidth();
  [[nodiscard]] int getViewportFullWidth();
  [[nodiscard]] int getViewportWidthSansAscii();
  [[nodiscard]] int getViewportWidthSansAsciiAndAddress();

protected:
  bool event(QEvent *event) override;
  void changeEvent(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool handleOverlayPaintEvent(QObject* obj, QPaintEvent* event) const;
  bool handleSelectedItemPaintEvent(QObject* obj, QPaintEvent* event);
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  int hexXOffset() const;
  int getVirtualHeight() const;
  void updateSize();
  int getTotalLines() const;
  int getOffsetFromPoint(QPoint pos) const;
  std::pair<QRect,QRect> calculateSelectionRectsForLine(int startColumn, int length, qreal dpr) const;
  void resizeOverlays(int y, int viewportHeight) const;
  void redrawOverlay();
  void printLine(QPainter& painter, int line) const;
  void printAddress(QPainter& painter, int line) const;
  void printData(QPainter& painter, int startAddress, int endAddress) const;
  void translateAndPrintHex(QPainter& painter,
                            const uint8_t* data,
                            int offset,
                            int length,
                            const QColor& bgColor,
                            const QColor& textColor) const;
  void printHex(QPainter& painter,
                const uint8_t* data,
                int length,
                const QColor& bgColor,
                const QColor& textColor) const;
  void translateAndPrintAscii(QPainter& painter,
                  const uint8_t* data,
                  int offset,
                  int length,
                  const QColor& bgColor,
                  const QColor& textColor) const;
  void printAscii(QPainter& painter,
                  const uint8_t* data,
                  int length,
                  const QColor& bgColor,
                  const QColor& textColor) const;
  void initAnimations();
  void showSelectedItem(bool show, bool animate);
  void drawSelectedItem() const;

  VGMFile* vgmfile;
  VGMItem* selectedItem;
  int selectedOffset;
  int charWidth;
  int charHalfWidth;
  int lineHeight;
  int ascent;
  bool addressAsHex = true;
  bool isDragging = false;
  bool shouldDrawOffset = true;
  bool shouldDrawAscii = true;
  int prevWidth = 0;
  int prevHeight = 0;

  int m_virtual_full_width{-1};
  int m_virtual_width_sans_ascii{-1};
  int m_virtual_width_sans_ascii_and_address{-1};

  QCache<int, QPixmap> lineCache;
  QGraphicsOpacityEffect* overlayOpacityEffect = nullptr;
  QGraphicsDropShadowEffect* selectedItemShadowEffect = nullptr;
  QParallelAnimationGroup* selectionAnimation = nullptr;
  QWidget* overlay;
  QWidget* selectionView = nullptr;
  QPixmap selectionViewPixmap;
  QPixmap selectionViewPixmapWithShadow;

signals:
  void selectionChanged(VGMItem* item);
};

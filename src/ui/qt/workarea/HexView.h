/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWidget>
#include <QCache>

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
  int getVirtualWidth() const;
  int getVirtualWidthSansAscii() const;
  int getVirtualWidthSansAsciiAndAddress() const;
  int getViewportWidth() const;
  int getViewportWidthSansAscii() const;
  int getViewportWidthSansAsciiAndAddress() const;

protected:
  bool event(QEvent *event) override;
  void changeEvent(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool handleOverlayPaintEvent(QObject* obj, const QEvent* event) const;
  bool handleSelectedItemPaintEvent(QObject* obj, QEvent* event);
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  int hexXOffset() const;
  int getVirtualHeight() const;
  int getTotalLines() const;
  int getOffsetFromPoint(QPoint pos) const;
  void resizeOverlays(int height) const;
  void redrawOverlay();
  void printLine(QPainter& painter, int line) const;
  void printAddress(QPainter& painter, int line) const;
  void printData(QPainter& painter, int startAddress, int endAddress) const;
  void translateAndPrintHex(QPainter& painter,
                            const uint8_t* data,
                            int offset,
                            int length,
                            QColor bgColor,
                            QColor textColor) const;
  void printHex(QPainter& painter,
                const uint8_t* data,
                int length,
                QColor bgColor,
                QColor textColor) const;
  void translateAndPrintAscii(QPainter& painter,
                  const uint8_t* data,
                  int offset,
                  int length,
                  QColor bgColor,
                  QColor textColor) const;
  void printAscii(QPainter& painter,
                  const uint8_t* data,
                  int length,
                  QColor bgColor,
                  QColor textColor) const;
  void showOverlay(bool show, bool animate);
  void drawSelectedItem() const;

  VGMFile* vgmfile;
  VGMItem* selectedItem;
  int selectedOffset;
  int charWidth;
  int lineHeight;
  bool addressAsHex = true;
  bool isDragging = false;
  bool showOffset = true;
  bool shouldDrawAscii = true;
  int prevWidth = 0;
  int prevHeight = 0;

  QCache<int, QPixmap> lineCache;
  QGraphicsOpacityEffect* overlayOpacityEffect = nullptr;
  QPropertyAnimation* overlayAnimation = nullptr;
  QWidget* overlay;
  QWidget* selectionView = nullptr;
  VGMItem* prevSelectedItem = nullptr;
  QPixmap selectionViewPixmap;

signals:
  void selectionChanged(VGMItem* item);
};

/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWidget>

class VGMFile;
class VGMItem;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

class HexView : public QWidget {
  Q_OBJECT
public:
  explicit HexView(VGMFile* vgmfile, QWidget *parent = nullptr);
  void setSelectedItem(VGMItem* item);
  void setFont(const QFont& font);

protected:
  void changeEvent(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool handleOverlayPaintEvent(QObject* obj, QEvent* event);
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent* event);

private:
  int getVirtualHeight();
  int getVirtualWidth();
  int getTotalLines();
  void resizeOverlays(int height);
  void redrawOverlay();
  void redrawSelectedItem();
  void printLine(QPainter& painter, int line);
  void printAddress(QPainter& painter, int line);
  void printData(QPainter& painter, int startAddress, int endAddress);
  void translateAndPrintHex(QPainter& painter,
                            uint8_t* data,
                            int offset,
                            int length,
                            QColor bgColor,
                            QColor textColor);
  void printHex(QPainter& painter,
                uint8_t* data,
                int length,
                QColor bgColor,
                QColor textColor);
  void translateAndPrintAscii(QPainter& painter,
                  uint8_t* data,
                  int offset,
                  int length,
                  QColor bgColor,
                  QColor textColor);
  void printAscii(QPainter& painter,
                  uint8_t* data,
                  int length,
                  QColor bgColor,
                  QColor textColor);
  void showOverlay(bool show, bool animate);
  void drawSelectedItem();

  VGMFile* vgmfile;
  VGMItem* selectedItem;
  int selectedOffset;
  int charWidth;
  int lineHeight;
  bool addressAsHex = true;

  QGraphicsOpacityEffect* overlayOpacityEffect = nullptr;
  QPropertyAnimation* overlayAnimation = nullptr;
  QWidget* overlay;
  QWidget* asciiOverlay;
  QWidget* selectionView = nullptr;
  QWidget* asciiSelectionView = nullptr;

signals:
  void selectionChanged(VGMItem* item);
};


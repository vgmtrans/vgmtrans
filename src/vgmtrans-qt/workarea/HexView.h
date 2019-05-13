/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QtWidgets>

class VGMFile;

class HexView : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit HexView(VGMFile *vgmfile, QWidget *parent = nullptr);

private:
  VGMFile *ui_hexview_vgmfile;
  int hexview_line_height;
  int hexview_line_ascent;
  int hexview_lines_per_screen = 0;

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
};

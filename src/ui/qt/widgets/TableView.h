/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QTableView>

class TableView : public QTableView {
  Q_OBJECT

public:
  explicit TableView(QWidget *parent = nullptr);
  virtual ~TableView() = default;

  void setModel(QAbstractItemModel *model) override;

protected:
  void scrollContentsBy(int dx, int dy) override;
  void resizeEvent(QResizeEvent *event) override;

protected slots:
  void onHeaderSectionResized(int index, int oldSize, int newSize);

private:
  void resizeColumns();

  bool headerSectionResizeLock = false;
};
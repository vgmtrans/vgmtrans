#pragma once


#include <QMainWindow>
#include <QSplitter.h>

class VGMCollView;
class VGMCollListView;

class VGMCollSplitter : public QSplitter {
 Q_OBJECT

 public:
  VGMCollSplitter(Qt::Orientation, QWidget* parent = Q_NULLPTR);
  ~VGMCollSplitter();

//  void addToMdi();

 protected:
  VGMCollView *collView;
  VGMCollListView *collListView;
};

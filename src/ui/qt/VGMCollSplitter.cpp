#include "VGMCollSplitter.h"
#include "VGMCollView.h"
#include "VGMCollListView.h"
#include "MdiArea.h"
#include "Helpers.h"


const int splitterHandleWidth = 1;


VGMCollSplitter::VGMCollSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent)
{
  collListView = new VGMCollListView(this);
  collView = new VGMCollView(collListView->selectionModel(), this);

  this->addWidget(collView);
  this->addWidget(collListView);

  this->setSizes(QList<int>() << 200 << 500);
  this->setCollapsible(0, false);
  this->setCollapsible(1, false);
//    this->setMinimumSize(700, 100);

  this->setStretchFactor(1, 1);
  this->setHandleWidth(splitterHandleWidth);
}

VGMCollSplitter::~VGMCollSplitter()
{
}

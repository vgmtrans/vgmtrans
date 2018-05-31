#include "VGMFileTreeView.h"

VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent)
        : QTreeView(parent)
        , vgmfile(file)
        , model(file)
{
    this->setModel(&model);

    setAttribute(Qt::WA_MacShowFocusRect, 0);


}

VGMFileTreeView::~VGMFileTreeView()
{
}
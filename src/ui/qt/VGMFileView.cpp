#include "VGMFileView.h"
#include "VGMFile.h"
#include "HexView.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "Helpers.h"


const int splitterHandleWidth = 1;


VGMFileView::VGMFileView(VGMFile *vgmFile)
        : QSplitter(Qt::Horizontal, 0)
{
    hexView = new HexView(vgmFile, this);
    treeView = new VGMFileTreeView(vgmFile, this);

    this->addWidget(hexView);
    this->addWidget(treeView);


    this->setSizes(QList<int>() << 500 << 100);
//    this->setMinimumSize(700, 100);

    this->setStretchFactor(1, 1);
    this->setHandleWidth(splitterHandleWidth);
    this->handle(1)->setDisabled(true);

    QString vgmFileName = QString::fromStdWString(*vgmFile->GetName());
    setWindowTitle(vgmFileName);
    setWindowIcon(iconForFileType(vgmFile->GetFileType()));
}

void VGMFileView::addToMdi() {
    MdiArea::getInstance()->addSubWindow(this);
    show();
}

VGMFileView::~VGMFileView()
{
}

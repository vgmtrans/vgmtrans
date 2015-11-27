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

//    horzSplitter->setMaximumSize(500, 0);
//    horzSplitter->setCollapsible(0, false);
//    horzSplitter->setCollapsible(1, false);
//    qDebug( "%d   %d  %d", parent->size().width(), this->size().width(), this->maximumSize().width() );

//    QList<int> sizes = QList<int>({100, 30});

    this->setSizes(QList<int>() << 500 << 100);
//    this->setMinimumSize(700, 100);

    this->setStretchFactor(1, 1);
//    this->setSizes(sizes);
    this->setHandleWidth(splitterHandleWidth);

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

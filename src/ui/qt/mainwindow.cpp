#include <qtextedit.h>
#include <QDragEnterEvent>
#include <QMimeData>
#include "mainwindow.h"
#include "QtVGMRoot.h"
#include "RawFileListView.h"
#include "VGMFileListView.h"
#include "VGMCollListView.h"
#include "MdiArea.h"

const int defaultWindowWidth = 800;
const int defaultWindowHeight = 600;
const int defaultCollListHeight = 140;
const int defaultFileListWidth = 200;
const int splitterHandleWidth = 1;

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent)
{
    setAcceptDrops(true);

    rawFileListView = new RawFileListView();
    vgmFileListView = new VGMFileListView();
    vgmCollListView = new VGMCollListView();

    vertSplitter = new QSplitter(Qt::Vertical, this);
    horzSplitter = new QSplitter(Qt::Horizontal, vertSplitter);
    vertSplitterLeft = new QSplitter(Qt::Vertical, horzSplitter);

    QList<int> sizes({defaultWindowHeight - defaultCollListHeight, defaultCollListHeight});
    vertSplitter->addWidget(horzSplitter);
    vertSplitter->addWidget(vgmCollListView);
    vertSplitter->setStretchFactor(0, 1);
    vertSplitter->setSizes(sizes);
    vertSplitter->setHandleWidth(splitterHandleWidth);
//    vertSplitter->setOpaqueResize(false);

    sizes = QList<int>({defaultFileListWidth, defaultWindowWidth - defaultFileListWidth});
    horzSplitter->addWidget(vertSplitterLeft);
    horzSplitter->addWidget(MdiArea::getInstance());
    horzSplitter->setStretchFactor(1, 1);
    horzSplitter->setSizes(sizes);
    horzSplitter->setHandleWidth(splitterHandleWidth);
    horzSplitter->setMinimumSize(100, 100);
    horzSplitter->setMaximumSize(500, 0);
    horzSplitter->setCollapsible(0, false);
    horzSplitter->setCollapsible(1, false);

    vertSplitterLeft->addWidget(rawFileListView);
    vertSplitterLeft->addWidget(vgmFileListView);
    vertSplitterLeft->setHandleWidth(splitterHandleWidth);

    setCentralWidget(vertSplitter);
    resize(defaultWindowWidth, defaultWindowHeight);
}

MainWindow::~MainWindow()
{

}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

wchar_t* qstringTowchar_t(QString text){
//    qDebug()<<text.length();
    wchar_t* c_Text = new wchar_t[text.length() + 1];
    text.toWCharArray(c_Text);

    c_Text[text.length()] = 0; //Add this line should work as you expected
    return c_Text;
}

void MainWindow::dropEvent(QDropEvent *event)
{

    const QMimeData *mimeData = event->mimeData();

//    if (mimeData->hasImage()) {
//        setPixmap(qvariant_cast<QPixmap>(mimeData->imageData()));
//    } else if (mimeData->hasHtml()) {
//        setText(mimeData->html());
//        setTextFormat(Qt::RichText);
//    } else
    if (mimeData->hasText()) {
        std::string utf8_text = mimeData->text().toUtf8().constData();
        printf(utf8_text.c_str());
//        setText(mimeData->text());
//        setTextFormat(Qt::PlainText);
    }
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        int urlSize = urlList.size();
        printf("%d", urlSize);
        QString text;
        for (int i = 0; i < urlList.size() && i < 32; ++i) {
            QString url = urlList.at(i).toLocalFile();
            wchar_t *str = qstringTowchar_t(url);
//            qDebug() << text.length();
            qtVGMRoot.OpenRawFile(str);
//            text += url + QString("\n");
        }
        printf(text.toUtf8().constData());
//        qtVGMRoot.OpenRawFile(urlList)
//        setText(text);
    } else {
//        setText(tr("Cannot display data"));
    }
//! [dropEvent() function part2]

//! [dropEvent() function part3]
    setBackgroundRole(QPalette::Dark);
    event->acceptProposedAction();

}

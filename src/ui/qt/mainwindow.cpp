#include <qtextedit.h>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDir>
#include <QFileDialog>
#include <QBoxLayout>
#include <QLabel>
#include <QFontDatabase>

#include <QDebug>

#include "MainWindow.h"
#include "Core.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "RawFileListView.h"
#include "VGMFileListView.h"
#include "VGMCollSplitter.h"
#include "HeaderContainer.h"
#include "MdiArea.h"
#include "MusicPlayer.h"

#include "VGMColl.h"
#include "VGMSeq.h"
#include "SF2File.h"
#include "MidiFile.h"

const int defaultWindowWidth = 800;
const int defaultWindowHeight = 600;
const int defaultCollListHeight = 140;
const int defaultFileListWidth = 200;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
  setWindowTitle("VGMTrans");
  setUnifiedTitleAndToolBarOnMac(true);
  setAcceptDrops(true);

  CreateComponents();
  SetupSplitters();

  ConnectMenuBar();
  ConnectToolBar();

  resize(defaultWindowWidth, defaultWindowHeight);
}

void MainWindow::CreateComponents()
{
  menuBar = new MenuBar(this);
  toolBar = new ToolBar(this);
  rawFileListView = new RawFileListView();
  vgmFileListView = new VGMFileListView();
  collSplitter = new VGMCollSplitter(Qt::Horizontal, this);
  rawFileListContainer = new HeaderContainer(rawFileListView, "Imported Files");
  vgmFileListContainer = new HeaderContainer(vgmFileListView, "Detected VGM Files");

  vertSplitter = new QSplitter(Qt::Vertical, this);
  horzSplitter = new QSplitter(Qt::Horizontal, vertSplitter);
  vertSplitterLeft = new QSplitter(Qt::Vertical, horzSplitter);
}

void MainWindow::SetupSplitters()
{
  QList<int> sizes({defaultWindowHeight - defaultCollListHeight, defaultCollListHeight});
  vertSplitter->addWidget(horzSplitter);
  vertSplitter->addWidget(collSplitter);
  vertSplitter->setStretchFactor(0, 1);
  vertSplitter->setSizes(sizes);

  sizes = QList<int>({defaultFileListWidth, defaultWindowWidth - defaultFileListWidth});
  horzSplitter->addWidget(vertSplitterLeft);
  horzSplitter->addWidget(MdiArea::getInstance());
  horzSplitter->setStretchFactor(1, 1);
  horzSplitter->setSizes(sizes);
  horzSplitter->setMinimumSize(100, 100);
  horzSplitter->setMaximumSize(500, 0);
  horzSplitter->setCollapsible(0, false);
  horzSplitter->setCollapsible(1, false);

  vertSplitterLeft->addWidget(rawFileListContainer);
  vertSplitterLeft->addWidget(vgmFileListContainer);

  setCentralWidget(vertSplitter);
}

void MainWindow::ConnectMenuBar()
{
  setMenuBar(menuBar);

  //File
  connect(menuBar, &MenuBar::Open, this, &MainWindow::Open);
  connect(menuBar, &MenuBar::Exit, this, &MainWindow::Exit);
}

void MainWindow::ConnectToolBar()
{
  addToolBar(toolBar);
  connect(toolBar, &ToolBar::OpenPressed, this, &MainWindow::Open);
  connect(toolBar, &ToolBar::PlayPressed, this, &MainWindow::Play);
  connect(toolBar, &ToolBar::PausePressed, this, &MainWindow::Pause);
  connect(toolBar, &ToolBar::StopPressed, this, &MainWindow::Stop);
}

void MainWindow::Open()
{
  QString file = QFileDialog::getOpenFileName(
      this, tr("Select a File"), QDir::currentPath(),
      tr("All Files (*)"));
  if (!file.isEmpty())
    core.OpenRawFile(file.toStdWString());
}

void MainWindow::Exit()
{
  core.Exit();
  this->close();
}

void MainWindow::Play()
{
  QModelIndexList list = vgmCollListView->selectionModel()->selectedIndexes();
  if (list.size() == 0 || list[0].row() >= core.vVGMColl.size())
    return;

  VGMColl* coll = core.vVGMColl[list[0].row()];
  VGMSeq* seq = coll->GetSeq();
  SF2File* sf2 = coll->CreateSF2File();
  MidiFile* midi = seq->ConvertToMidi();

  std::vector<uint8_t> midiBuf;
  midi->WriteMidiToBuffer(midiBuf);

  const void* rawSF2 = sf2->SaveToMem();

  MusicPlayer& musicPlayer = MusicPlayer::getInstance();

  musicPlayer.Stop();
  musicPlayer.LoadSF2(rawSF2);
  musicPlayer.Play(&midiBuf[0], midiBuf.size());

  delete[] rawSF2;
  delete sf2;
  delete midi;
}

void MainWindow::Pause()
{
  MusicPlayer& musicPlayer = MusicPlayer::getInstance();
  musicPlayer.Pause();
}

void MainWindow::Stop()
{
  MusicPlayer& musicPlayer = MusicPlayer::getInstance();
  musicPlayer.Stop();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
  event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
  event->acceptProposedAction();
}



void MainWindow::dropEvent(QDropEvent *event)
{
  const QMimeData *mimeData = event->mimeData();

  if (mimeData->hasText()) {
    std::string utf8_text = mimeData->text().toUtf8().constData();
    printf(utf8_text.c_str());
  }
  if (mimeData->hasUrls()) {
    QList<QUrl> urlList = mimeData->urls();
    int urlSize = urlList.size();
    printf("%d", urlSize);
    QString text;
    for (int i = 0; i < urlList.size(); ++i) {
      QString url = urlList.at(i).toLocalFile();
      core.OpenRawFile(url.toStdWString());
    }
    printf(text.toUtf8().constData());
  }

  setBackgroundRole(QPalette::Dark);
  event->acceptProposedAction();
}

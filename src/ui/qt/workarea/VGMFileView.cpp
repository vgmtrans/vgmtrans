/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include <QShortcut>
#include <QFontDatabase>
#include <QRawFont>
#include <VGMFile.h>
#include "qhexview/qhexview.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "Helpers.h"
#include "QtVGMRoot.h"

const int splitterHandleWidth = 1;

VGMFileView::VGMFileView(VGMFile *vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile), m_hexview(new QHexView) {
  m_splitter = new QSplitter(Qt::Horizontal, this);

  auto document = new QHexDocument(vgmfile);
  document->setBaseAddress(m_vgmfile->dwOffset);
  m_hexview->setDocument(document);

  int id = QFontDatabase::addApplicationFont(":/fonts/Ubuntu_Mono/UbuntuMono-Regular.ttf");
  QString family = QFontDatabase::applicationFontFamilies(id).at(0);
  QFont fixedFont(family);
  fixedFont.setFixedPitch(true);
  fixedFont.setPointSizeF(10.5);

  m_hexview->setFont(fixedFont);
  markEvents();

  setWindowTitle(QString::fromStdWString(*m_vgmfile->GetName()));
  setWindowIcon(iconForFileType(m_vgmfile->GetFileType()));
  setAttribute(Qt::WA_DeleteOnClose);

  m_treeview = new VGMFileTreeView(m_vgmfile, this);

  m_splitter->addWidget(m_hexview);
  m_splitter->addWidget(m_treeview);
  m_splitter->setSizes(QList<int>() << 900 << 270);

  connect(m_treeview, &VGMFileTreeView::currentItemChanged,
          [&](QTreeWidgetItem *item, QTreeWidgetItem *) {
            auto vgmitem = static_cast<VGMItem *>(item->data(0, Qt::UserRole).value<void *>());
            m_hexview->setSelectedItem(vgmitem);
          });

  connect(m_hexview->cursor(), &QHexCursor::positionChanged, [&]() {
    const QHexCursor *cursor = m_hexview->cursor();
    const auto &base_offset = m_vgmfile->dwOffset;
    VGMItem *item = m_vgmfile->GetItemFromOffset(base_offset + cursor->position().offset(), false);
    if (item) {
      auto widget_item = m_treeview->getTreeWidgetItem(item);
      m_treeview->setCurrentItem(widget_item);
    } else {
      m_treeview->clearSelection();
    }
  });

  // TODO: zoom
  
  setWidget(m_splitter);
}

void VGMFileView::markEvents() {
  auto base_offset = m_vgmfile->dwOffset;
  auto overlay = m_hexview->document()->metadata();
  uint32_t i = 0;
  while (i < m_vgmfile->unLength) {
    auto item = m_vgmfile->GetItemFromOffset(base_offset + i, false);
    if (item) {
      auto item_offset = item->dwOffset - base_offset;  // offset from start of hexdocument
      auto desc = QString::fromStdWString(item->GetDescription());
      overlay->metadata(item_offset, item_offset + item->unLength,
                        textColorForEventColor(item->color), colorForEventColor(item->color), desc);
      i += item->unLength;
    } else {
      i++;
    }
  }
}

void VGMFileView::closeEvent(QCloseEvent *) {
  MdiArea::the()->removeView(m_vgmfile);
}

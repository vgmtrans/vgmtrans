/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include <QShortcut>
#include <QFontDatabase>
#include <VGMFile.h>
#include <qhexview.h>
#include <document/buffer/qmemoryrefbuffer.h>
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "Helpers.h"

const int splitterHandleWidth = 1;

VGMFileView::VGMFileView(VGMFile *vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile), m_hexview(new QHexView) {
  m_splitter = new QSplitter(Qt::Horizontal, this);

  /* this copy is nasty, removing it involves some serious backend refactoring... */
  auto &internal_buffer = m_buffer.buffer();
  internal_buffer.resize(m_vgmfile->unLength);
  m_vgmfile->GetBytes(m_vgmfile->dwOffset, m_vgmfile->unLength, internal_buffer.data());
  m_buffer.open(QIODevice::ReadOnly);

  auto document = QHexDocument::fromDevice<QMemoryRefBuffer>(&m_buffer);
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
          [file_ofs = m_vgmfile->dwOffset, hexview = m_hexview](
              QTreeWidgetItem *item, [[maybe_unused]] QTreeWidgetItem *) {
            auto vgmitem = static_cast<VGMItem *>(item->data(0, Qt::UserRole).value<void *>());
            auto offset = vgmitem->dwOffset - file_ofs;

            hexview->document()->cursor()->selectOffset(offset, vgmitem->unLength);
          });

  connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated,
          [hexview = m_hexview]() {
            auto font = hexview->font();
            font.setPointSizeF(font.pointSizeF() + 0.5);
            hexview->setFont(font);
          });

  connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated,
          [hexview = m_hexview]() {
            auto font = hexview->font();
            font.setPointSizeF(font.pointSizeF() - 0.5);
            hexview->setFont(font);
          });

  setWidget(m_splitter);
}

void VGMFileView::markEvents() {
  auto base_offset = m_vgmfile->dwOffset;
  auto overlay = m_hexview->document()->metadata();
  uint32_t i = 0;
  while (i < m_vgmfile->unLength) {
    auto item = m_vgmfile->GetItemFromOffset(base_offset + i, false);
    if (item) {
      auto line = std::floor((item->dwOffset - base_offset) / 16);
      auto col = (item->dwOffset - base_offset) % 16;
      auto desc = QString::fromStdWString(item->GetDescription());
      overlay->metadata(line, col, item->unLength, textColorForEventColor(item->color),
                        colorForEventColor(item->color), desc);
      i += item->unLength;
    } else {
      i++;
    }
  }
}

void VGMFileView::closeEvent(QCloseEvent *) {
  MdiArea::the()->removeView(m_vgmfile);
}

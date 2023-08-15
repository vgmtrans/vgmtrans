/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include <QApplication>
#include <QShortcut>
#include <QFont>
#include <QScrollArea>
#include <VGMFile.h>
#include "HexView.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "Helpers.h"
#include <QScrollBar>

VGMFileView::VGMFileView(VGMFile *vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile), m_hexview(new HexView(vgmfile)) {
  m_splitter = new QSplitter(Qt::Horizontal, this);

  setWindowTitle(QString::fromStdWString(*m_vgmfile->GetName()));
  setWindowIcon(iconForFileType(m_vgmfile->GetFileType()));
  setAttribute(Qt::WA_DeleteOnClose);

  m_treeview = new VGMFileTreeView(m_vgmfile, this);

  m_hexScrollArea = new QScrollArea;
  m_hexScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_hexScrollArea->setWidgetResizable(true);
  m_hexScrollArea->setWidget(m_hexview);
  m_hexScrollArea->show();

  m_splitter->addWidget(m_hexScrollArea);
  m_splitter->addWidget(m_treeview);
  m_splitter->setSizes(QList<int>() << 900 << 270);


  connect(m_hexview, &HexView::selectionChanged, this, &VGMFileView::onSelectionChange);

  connect(m_treeview, &VGMFileTreeView::currentItemChanged,
          [&](QTreeWidgetItem *item, QTreeWidgetItem *) {
            if (item == nullptr) {
              return;
            }
            auto vgmitem = static_cast<VGMItem *>(item->data(0, Qt::UserRole).value<void *>());
            onSelectionChange(vgmitem);
          });

  connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated, [&] {
    auto font = m_hexview->font();
    font.setPointSizeF(font.pointSizeF() + 0.5);
    m_hexview->setFont(font);
  });

  connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated, [&] {
    auto font = m_hexview->font();
    font.setPointSizeF(font.pointSizeF() - 0.5);
    m_hexview->setFont(font);
  });

  setWidget(m_splitter);
}

void VGMFileView::closeEvent(QCloseEvent *) {
  MdiArea::the()->removeView(m_vgmfile);
}

void VGMFileView::onSelectionChange(VGMItem *item) {
  m_hexview->setSelectedItem(item);
  if (item) {
    auto widget_item = m_treeview->getTreeWidgetItem(item);
    m_treeview->blockSignals(true);
    m_treeview->setCurrentItem(widget_item);
    m_treeview->blockSignals(false);
  } else {
    m_treeview->clearSelection();
  }
}
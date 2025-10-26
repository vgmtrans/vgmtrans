/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QShortcut>
#include <QScrollArea>
#include <QScrollBar>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include "VGMFileView.h"
#include "VGMFile.h"
#include "HexView.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "SnappingSplitter.h"
#include "Helpers.h"
#include "Root.h"

VGMFileView::VGMFileView(VGMFile *vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile), m_hexview(new HexView(vgmfile)) {
  m_splitter = new SnappingSplitter(Qt::Horizontal, this);

  setWindowTitle(QString::fromStdString(m_vgmfile->name()));
  setWindowIcon(iconForFile(vgmFileToVariant(vgmfile)));
  setAttribute(Qt::WA_DeleteOnClose);

  m_treeview = new VGMFileTreeView(m_vgmfile, this);

  m_hexScrollArea = new QScrollArea;
  m_hexScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_hexScrollArea->horizontalScrollBar()->setEnabled(false);
  m_hexScrollArea->setWidgetResizable(true);
  m_hexScrollArea->setWidget(m_hexview);

  m_splitter->addWidget(m_hexScrollArea);
  m_splitter->addWidget(m_treeview);
  m_splitter->setSizes(QList<int>{hexViewFullWidth(), treeViewMinimumWidth});
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->persistState();
  resetSnapRanges();
  m_hexScrollArea->setMaximumWidth(hexViewFullWidth());
  m_treeview->setMinimumWidth(treeViewMinimumWidth);

  m_defaultHexFont = m_hexview->font();

  connect(m_hexview, &HexView::selectionChanged, this, &VGMFileView::onSelectionChange);

  connect(m_treeview, &VGMFileTreeView::currentItemChanged,
          [&](const QTreeWidgetItem *item, QTreeWidgetItem *) {
            if (item == nullptr) {
              // If the VGMFileTreeView deselected, then so should the HexView
              onSelectionChange(nullptr);
              return;
            }
            auto vgmitem = static_cast<VGMItem *>(item->data(0, Qt::UserRole).value<void *>());
            onSelectionChange(vgmitem);
          });

  connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated,
          this, &VGMFileView::increaseHexViewFont);

  auto *shortcutEqual = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
  connect(shortcutEqual, &QShortcut::activated, this, &VGMFileView::increaseHexViewFont);

  connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated,
          this, &VGMFileView::decreaseHexViewFont);

  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated,
          this, &VGMFileView::resetHexViewFont);

  setWidget(m_splitter);
}

void VGMFileView::focusInEvent(QFocusEvent* event) {
  QMdiSubWindow::focusInEvent(event);

  m_treeview->updateStatusBar();
}

void VGMFileView::resetSnapRanges() const {
  m_splitter->clearSnapRanges();
  m_splitter->addSnapRange(0, hexViewWidthSansAsciiAndAddress(), hexViewWidthSansAscii());
  m_splitter->addSnapRange(0, hexViewWidthSansAscii(), hexViewFullWidth());
}

int VGMFileView::hexViewFullWidth() const {
  return m_hexview->getViewportFullWidth();
}

int VGMFileView::hexViewWidthSansAscii() const {
  return m_hexview->getViewportWidthSansAscii();
}

int VGMFileView::hexViewWidthSansAsciiAndAddress() const {
  return m_hexview->getViewportWidthSansAsciiAndAddress();
}

void VGMFileView::updateHexViewFont(qreal sizeIncrement) const {
  // Increment the font size until it has an actual effect on width
  QFont font = m_hexview->font();
  QFontMetricsF fontMetrics(font);
  const qreal origWidth = fontMetrics.horizontalAdvance("A");
  qreal fontSize = font.pointSizeF();
  for (int i = 0; i < 3; i++) {
    fontSize += sizeIncrement;
    font.setPointSizeF(fontSize);
    fontMetrics = QFontMetricsF(font);
    if (!qFuzzyCompare(fontMetrics.horizontalAdvance("A"), origWidth)) {
      break;
    }
  }

  applyHexViewFont(font);
}

void VGMFileView::applyHexViewFont(QFont font) const {
  const QList<int> splitterSizes = m_splitter->sizes();
  const int actualWidthBeforeResize = splitterSizes.isEmpty() ? hexViewFullWidth() : splitterSizes.first();
  const int fullWidthBeforeResize = std::max(1, hexViewFullWidth());

  m_hexview->setFont(font);
  m_hexScrollArea->setMaximumWidth(hexViewFullWidth());

  const float percentHexViewVisible = static_cast<float>(actualWidthBeforeResize) /
                                      static_cast<float>(fullWidthBeforeResize);
  const int fullWidthAfterResize = std::max(1, hexViewFullWidth());
  const int widthChange = fullWidthAfterResize - fullWidthBeforeResize;
  const auto scaledWidthChange = static_cast<int>(std::round(static_cast<float>(widthChange) * percentHexViewVisible));
  const int newWidth = std::max(1, actualWidthBeforeResize + scaledWidthChange);
  resetSnapRanges();
  m_splitter->setSizes(QList<int>{newWidth, treeViewMinimumWidth});
  m_splitter->persistState();
}

void VGMFileView::resetHexViewFont() {
  applyHexViewFont(m_defaultHexFont);
}

void VGMFileView::increaseHexViewFont() {
  updateHexViewFont(+0.5);
}

void VGMFileView::decreaseHexViewFont() {
  updateHexViewFont(-0.5);
}

void VGMFileView::closeEvent(QCloseEvent *) {
  MdiArea::the()->removeView(m_vgmfile);
}

void VGMFileView::onSelectionChange(VGMItem *item) const {
  m_hexview->setSelectedItem(item);
  if (item) {
    auto widget_item = m_treeview->getTreeWidgetItem(item);
    m_treeview->blockSignals(true);
    m_treeview->setCurrentItem(widget_item);
    m_treeview->blockSignals(false);
  } else {
    m_treeview->setCurrentItem(nullptr);
    m_treeview->clearSelection();
  }
}

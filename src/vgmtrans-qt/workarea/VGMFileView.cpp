/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include "VGMFile.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"

#include <QTreeWidgetItem>
#include <QBuffer>

#include "QHexView/qhexview.h"
#include "QHexView/document/buffer/qmemoryrefbuffer.h"
#include "QHexView/document/qhexmetadata.h"

#include "../util/Helpers.h"

VGMFileView::VGMFileView(VGMFile *vgmfile) : QMdiSubWindow() {
    m_vgmfile = vgmfile;
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_buffer = new QBuffer();
    m_buffer->setData(vgmfile->data(), vgmfile->size());
    m_buffer->open(QIODevice::ReadOnly);
    m_hexview = new QHexView(m_splitter);

    auto doc = QHexDocument::fromDevice<QMemoryRefBuffer>(m_buffer);
    doc->setBaseAddress(vgmfile->dwOffset);
    m_hexview->setDocument(doc);
    m_hexview->setReadOnly(true);
    markEvents();

    m_treeview = new VGMFileTreeView(m_vgmfile, m_splitter);
    m_splitter->setSizes(QList<int>() << 900 << 270);

    setWindowTitle(QString::fromStdString(*m_vgmfile->GetName()));
    setWindowIcon(iconForFileType(m_vgmfile->GetFileType()));
    setAttribute(Qt::WA_DeleteOnClose);

    connect(m_treeview, &VGMFileTreeView::itemClicked, [=](QTreeWidgetItem *item, int) {
        auto vgmitem = reinterpret_cast<VGMTreeItem *>(item);
        auto offset = vgmitem->item_offset() - vgmfile->dwOffset;

        m_hexview->moveTo(offset);
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
            auto item_len = item->unLength;
            auto desc = QString::fromStdString(item->GetDescription());
            while (col + item_len > 16) {
                auto part_len = 16 - col;
                overlay->metadata(line, col, part_len, textColorForEventColor(item->color),
                                  colorForEventColor(item->color), desc);
                line++;
                col = 0;
                item_len -= part_len;
            }
            overlay->metadata(line, col, item_len, textColorForEventColor(item->color),
                              colorForEventColor(item->color), desc);
            i += item->unLength;
        } else {
            i++;
        }
    }
}

void VGMFileView::highlightItem(QTreeWidgetItem *item, int) {
    auto vgmitem = static_cast<VGMTreeItem *>(item);
}

void VGMFileView::closeEvent(QCloseEvent *) {
    MdiArea::Instance()->RemoveView(m_vgmfile);
}

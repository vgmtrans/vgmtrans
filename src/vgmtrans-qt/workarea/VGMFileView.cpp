/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include "VGMFile.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"

#include <QHBoxLayout>

#include "QHexView/qhexview.h"
#include "QHexView/document/buffer/qmemoryrefbuffer.h"
#include "QHexView/document/qhexmetadata.h"

#include "../util/Helpers.h"

#include <QBuffer>

VGMFileView::VGMFileView(VGMFile *vgmfile) : QMdiSubWindow() {
    m_vgmfile = vgmfile;
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_buffer = new QBuffer();
    if(!vgmfile->bUsingRawFile) {
        m_buffer->setData(reinterpret_cast<const char *>(vgmfile->rawData()), vgmfile->unLength);
    } else {
        auto tmpbuf = new char[vgmfile->unLength];
        vgmfile->GetBytes(vgmfile->dwOffset, vgmfile->unLength, tmpbuf);
        m_buffer->setData(tmpbuf, vgmfile->unLength);
    }
    m_buffer->open(QIODevice::ReadOnly);
    m_hexview =  new QHexView(m_splitter);
    auto doc = QHexDocument::fromDevice<QMemoryRefBuffer>(m_buffer);
    doc->setBaseAddress(vgmfile->dwOffset);
    m_hexview->setDocument(doc);
    m_hexview->setReadOnly(true);
    markEvents();

    m_treeview = new VGMFileTreeView(m_vgmfile, m_splitter);
    m_splitter->setSizes(QList<int>() << 900 << 270);

    setWindowTitle(QString::fromStdWString(*m_vgmfile->GetName()));
    setWindowIcon(iconForFileType(m_vgmfile->GetFileType()));
    setAttribute(Qt::WA_DeleteOnClose);

    setWidget(m_splitter);
}

void VGMFileView::markEvents() {
    auto base_offset = m_vgmfile->dwOffset;
    auto overlay = m_hexview->document()->metadata();

    uint32_t i = 0;
    while(i < m_vgmfile->unLength) {
        auto item = m_vgmfile->GetItemFromOffset(base_offset + i, false);
        if(item) {
            auto line = std::floor((item->dwOffset - base_offset) / 16);
            auto col = (item->dwOffset - base_offset) % 16;
            auto item_len = item->unLength;
            while (col + item_len > 16) {
                auto part_len = 16 - col;
                overlay->color(line, col, part_len, textColorForEventColor(item->color), colorForEventColor(item->color));
                line++;
                col = 0;
                item_len -= part_len;
            }
            overlay->color(line, col, item_len, textColorForEventColor(item->color), colorForEventColor(item->color));
            i += item->unLength;
        } else {
            i++;
        }
    }
}

void VGMFileView::closeEvent(QCloseEvent *) {
    MdiArea::Instance()->RemoveView(m_vgmfile);
}
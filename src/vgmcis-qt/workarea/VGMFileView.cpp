/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include "VGMFile.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"

#include <QTreeWidgetItem>
#include <QBuffer>
#include <cmath>

#include "QHexView/qhexview.h"
#include "QHexView/document/buffer/qmemoryrefbuffer.h"

#include "../util/Helpers.h"

#include "components/seq/VGMSeq.h"
#include "components/instr/VGMInstrSet.h"
#include "components/VGMSampColl.h"
#include "components/VGMMiscFile.h"

VGMFileView::VGMFileView(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> vgmfile)
        : QMdiSubWindow() {
    m_vgmfile = vgmfile;
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_buffer = new QBuffer();
    std::visit([&](auto file) {
        m_buffer->setData(file->data(), file->size());
        m_buffer->open(QIODevice::ReadOnly);
    }, vgmfile);
    m_hexview = new QHexView(m_splitter);

    auto doc = QHexDocument::fromDevice<QMemoryRefBuffer>(m_buffer);
    auto file_ofs = std::visit([&](auto file) {
        return file->dwOffset;
    }, vgmfile);
    doc->setBaseAddress(file_ofs);
    m_hexview->setDocument(doc);
    m_hexview->setReadOnly(true);
    markEvents();

    m_treeview = new VGMFileTreeView(std::visit([](auto file) ->VGMFile* { return file; }, vgmfile), m_splitter);
    m_splitter->setSizes(QList<int>() << 900 << 270);

    setWindowTitle(QString::fromStdString(*std::visit([&](auto file) { return file->GetName(); }, vgmfile)));
    static Visitor icon{
            [](VGMSeq *) -> const QIcon & {
                static QIcon i_gen{":/images/sequence.svg"};
                return i_gen;
            },
            [](VGMInstrSet *) -> const QIcon & {
                static QIcon i_gen{":/images/instrument-set.svg"};
                return i_gen;
            },
            [](VGMSampColl *) -> const QIcon & {
                static QIcon i_gen{":/images/wave.svg"};
                return i_gen;
            },
            [](VGMMiscFile *) -> const QIcon & {
                static QIcon i_gen{":/images/file.svg"};
                return i_gen;
            },
    };
    setWindowIcon(std::visit(icon, vgmfile));
    setAttribute(Qt::WA_DeleteOnClose);

    connect(m_treeview, &VGMFileTreeView::itemClicked, [=](QTreeWidgetItem *item, int) {
        auto vgmitem = reinterpret_cast<VGMTreeItem *>(item);
        auto offset = vgmitem->item_offset() - file_ofs;

        m_hexview->moveTo(offset);
    });

    setWidget(m_splitter);
}

void VGMFileView::markEvents() {
    auto [base_offset, file_len] = std::visit(
            [](auto file) { return std::pair<size_t, size_t>{file->dwOffset, file->unLength}; }, m_vgmfile);
    auto overlay = m_hexview->document()->metadata();

    size_t i = 0;
    while (i < file_len) {
        auto item = std::visit(
                [base_offset = base_offset, i](auto file) { return file->GetItemFromOffset(base_offset + i, false); },
                m_vgmfile);
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

void VGMFileView::closeEvent(QCloseEvent *) {
    MdiArea::Instance()->RemoveView(m_vgmfile);
}

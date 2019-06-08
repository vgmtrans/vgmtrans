#include "qhexview.h"
#include "document/buffer/qmemorybuffer.h"
#include <QFontDatabase>
#include <QApplication>
#include <QPaintEvent>
#include <QHelpEvent>
#include <QScrollBar>
#include <QPainter>
#include <QToolTip>
#include <cmath>

#define CURSOR_BLINK_INTERVAL 500 // ms
#define DOCUMENT_WHEEL_LINES  3

QHexView::QHexView(QWidget *parent) : QAbstractScrollArea(parent), m_document(nullptr), m_renderer(nullptr), m_readonly(false)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    if(!(f.styleHint() & QFont::Monospace))
    {
        f.setFamily("Monospace"); // Force Monospaced font
        f.setStyleHint(QFont::TypeWriter);
    }

    this->setFont(f);
    this->setFocusPolicy(Qt::StrongFocus);
    this->setMouseTracking(true);
    this->verticalScrollBar()->setSingleStep(1);
    this->verticalScrollBar()->setPageStep(1);

    m_blinktimer = new QTimer(this);
    m_blinktimer->setInterval(CURSOR_BLINK_INTERVAL);

    connect(m_blinktimer, &QTimer::timeout, this, &QHexView::blinkCursor);

    this->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(QByteArray(), this));
}

QHexDocument *QHexView::document() { return m_document; }

void QHexView::setDocument(QHexDocument *document)
{
    if(m_renderer)
        m_renderer->deleteLater();

    if(m_document)
        m_document->deleteLater();

    m_document = document;
    m_renderer = new QHexRenderer(m_document, this->fontMetrics(), this);

    connect(m_document, &QHexDocument::documentChanged, this, [&]() {
        this->adjustScrollBars();
        this->viewport()->update();
    });

    connect(m_document->cursor(), &QHexCursor::positionChanged, this, &QHexView::moveToSelection);
    connect(m_document->cursor(), &QHexCursor::insertionModeChanged, this, &QHexView::renderCurrentLine);

    this->adjustScrollBars();
    this->viewport()->update();
}

void QHexView::setReadOnly(bool b)
{
    m_readonly = b;

    if(m_document)
        m_document->cursor()->setInsertionMode(QHexCursor::OverwriteMode);
}

bool QHexView::event(QEvent *e)
{
    if(m_document && m_renderer && (e->type() == QEvent::ToolTip))
    {
        QHelpEvent* helpevent = static_cast<QHelpEvent*>(e);
        QHexPosition position;

        if(m_renderer->hitTest(helpevent->pos(), &position, this->firstVisibleLine()))
        {
            QString comments = m_document->metadata()->comments(position.line);

            if(!comments.isEmpty())
                QToolTip::showText(helpevent->globalPos(), comments, this);
        }

        return true;
    }

    return QAbstractScrollArea::event(e);
}

void QHexView::scrollContentsBy(int dx, int dy)
{
    if(dx)
    {
        QWidget* viewport = this->viewport();
        viewport->move(viewport->x() + dx, viewport->y());
        return;
    }

    QAbstractScrollArea::scrollContentsBy(dx, dy);
}

void QHexView::keyPressEvent(QKeyEvent *e)
{
    if(!m_renderer || !m_document)
    {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }

    QHexCursor* cur = m_document->cursor();

    m_blinktimer->stop();
    m_renderer->enableCursor();

    bool handled = this->processMove(cur, e);
    if(!handled) handled = this->processAction(cur, e);
    if(!handled) handled = this->processTextInput(cur, e);
    if(!handled) QAbstractScrollArea::keyPressEvent(e);

    m_blinktimer->start();
}

void QHexView::mousePressEvent(QMouseEvent *e)
{
    QAbstractScrollArea::mousePressEvent(e);

    if(!m_renderer || (e->buttons() != Qt::LeftButton))
        return;

    QHexPosition position;

    if(!m_renderer->hitTest(e->pos(), &position, this->firstVisibleLine()))
        return;

    m_renderer->selectArea(e->pos());
    m_document->cursor()->moveTo(position);
    e->accept();
}

void QHexView::mouseMoveEvent(QMouseEvent *e)
{
    QAbstractScrollArea::mouseMoveEvent(e);

    if(!m_renderer || !m_document)
        return;

    if(e->buttons() == Qt::LeftButton)
    {
        if(m_blinktimer->isActive())
        {
            m_blinktimer->stop();
            m_renderer->enableCursor(false);
        }

        QHexCursor* cursor = m_document->cursor();
        QHexPosition position;

        if(!m_renderer->hitTest(e->pos(), &position, this->firstVisibleLine()))
            return;

        cursor->select(position.line, position.column, 0);
        e->accept();
    }

    if(e->buttons() != Qt::NoButton)
        return;

    int hittest = m_renderer->hitTestArea(e->pos());

    if((hittest == QHexRenderer::AddressArea) || (hittest == QHexRenderer::ExtraArea))
        this->setCursor(Qt::ArrowCursor);
    else
        this->setCursor(Qt::IBeamCursor);
}

void QHexView::mouseReleaseEvent(QMouseEvent *e)
{
   QAbstractScrollArea::mouseReleaseEvent(e);

   if(e->button() != Qt::LeftButton)
       return;

   if(!m_blinktimer->isActive())
       m_blinktimer->start();

   e->accept();
}

void QHexView::focusInEvent(QFocusEvent *e)
{
    QAbstractScrollArea::focusInEvent(e);

    if(!m_renderer)
        return;

    m_renderer->enableCursor();
    m_blinktimer->start();
}

void QHexView::focusOutEvent(QFocusEvent *e)
{
    QAbstractScrollArea::focusOutEvent(e);

    if(!m_renderer)
        return;

    m_blinktimer->stop();
    m_renderer->enableCursor(false);
}

void QHexView::wheelEvent(QWheelEvent *e)
{
    if(e->orientation() == Qt::Vertical)
    {
        int value = this->verticalScrollBar()->value();

        if(e->delta() < 0) // Scroll Down
            this->verticalScrollBar()->setValue(value + DOCUMENT_WHEEL_LINES);
        else if(e->delta() > 0) // Scroll Up
            this->verticalScrollBar()->setValue(value - DOCUMENT_WHEEL_LINES);

        return;
    }

    QAbstractScrollArea::wheelEvent(e);
}

void QHexView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    this->adjustScrollBars();
}

void QHexView::paintEvent(QPaintEvent *e)
{
    if(!m_document)
        return;

    QPainter painter(this->viewport());
    painter.setFont(this->font());

    const QRect& r = e->rect();
    int firstvisible = this->firstVisibleLine();
    int first = firstvisible + (r.top() / m_renderer->lineHeight());
    int last = firstvisible + (r.bottom() / m_renderer->lineHeight());
    int count = (last - first) + 1;

    m_renderer->render(&painter, first, count, firstvisible);
    m_renderer->renderFrame(&painter);
}

void QHexView::moveToSelection()
{
    QHexCursor* cur = m_document->cursor();

    if(!this->isLineVisible(cur->currentLine()))
    {
        QScrollBar* vscrollbar = this->verticalScrollBar();
        vscrollbar->setValue(std::max(0, cur->currentLine() - this->visibleLines() / 2));
    }
    else
        this->viewport()->update();
}

void QHexView::blinkCursor()
{
    if(!m_renderer)
        return;

    m_renderer->blinkCursor();
    this->renderCurrentLine();
}

void QHexView::moveNext(bool select)
{
    QHexCursor* cur = m_document->cursor();
    int line = cur->currentLine(), column = cur->currentColumn();
    bool lastcell = (line >= m_renderer->documentLastLine()) && (column >= m_renderer->documentLastColumn());

    if((m_renderer->selectedArea() == QHexRenderer::AsciiArea) && lastcell)
        return;

    int nibbleindex = cur->currentNibble();

    if(m_renderer->selectedArea() == QHexRenderer::HexArea)
    {
        if(lastcell && !nibbleindex)
            return;

        if(cur->position().offset() >= m_document->length() && nibbleindex)
            return;
    }

    if((m_renderer->selectedArea() == QHexRenderer::HexArea))
    {
        nibbleindex = (++nibbleindex % 2);

        if(nibbleindex)
            column++;
    }
    else
    {
        nibbleindex = 1;
        column++;
    }

    if(column > HEX_LINE_LAST_COLUMN)
    {
        line = std::min(m_renderer->documentLastLine(), line + 1);
        column = 0;
        nibbleindex = 1;
    }

    if(select)
        cur->select(line, std::min(HEX_LINE_LAST_COLUMN, column), nibbleindex);
    else
        cur->moveTo(line, std::min(HEX_LINE_LAST_COLUMN, column), nibbleindex);
}

void QHexView::movePrevious(bool select)
{
    QHexCursor* cur = m_document->cursor();
    int line = cur->currentLine(), column = cur->currentColumn();
    bool firstcell = !line && !column;

    if((m_renderer->selectedArea() == QHexRenderer::AsciiArea) && firstcell)
        return;

    int nibbleindex = cur->currentNibble();

    if((m_renderer->selectedArea() == QHexRenderer::HexArea) && firstcell && nibbleindex)
        return;

    if((m_renderer->selectedArea() == QHexRenderer::HexArea))
    {
        nibbleindex = (--nibbleindex % 2);

        if(!nibbleindex)
            column--;
    }
    else
    {
        nibbleindex = 1;
        column--;
    }

    if(column < 0)
    {
        line = std::max(0, line - 1);
        column = HEX_LINE_LAST_COLUMN;
        nibbleindex = 0;
    }

    if(select)
        cur->select(line, std::max(0, column), nibbleindex);
    else
        cur->moveTo(line, std::max(0, column), nibbleindex);
}

void QHexView::renderCurrentLine()
{
    if(!m_document)
        return;

    this->renderLine(m_document->cursor()->currentLine());
}

bool QHexView::processAction(QHexCursor *cur, QKeyEvent *e)
{
   if(m_readonly)
       return false;

   if(e->modifiers() != Qt::NoModifier)
   {
       if(e->matches(QKeySequence::SelectAll))
       {
           m_document->cursor()->moveTo(0, 0);
           m_document->cursor()->select(m_renderer->documentLastLine(), m_renderer->documentLastColumn() - 1);
       }
       else if(e->matches(QKeySequence::Undo))
           m_document->undo();
       else if(e->matches(QKeySequence::Redo))
           m_document->redo();
       else if(e->matches(QKeySequence::Cut))
           m_document->cut((m_renderer->selectedArea() == QHexRenderer::HexArea));
       else if(e->matches(QKeySequence::Copy))
           m_document->copy((m_renderer->selectedArea() == QHexRenderer::HexArea));
       else if(e->matches(QKeySequence::Paste))
           m_document->paste((m_renderer->selectedArea() == QHexRenderer::HexArea));
       else
           return false;

       return true;
   }

   if((e->key() == Qt::Key_Backspace) || (e->key() == Qt::Key_Delete))
   {
       if(!cur->hasSelection())
       {
           const QHexPosition& pos = cur->position();

           if(pos.offset() <= 0)
               return true;

           if(e->key() == Qt::Key_Backspace)
               m_document->remove(cur->position().offset() - 1, 1);
           else
               m_document->remove(cur->position().offset(), 1);
       }
       else
       {
           QHexPosition oldpos = cur->selectionStart();
           m_document->removeSelection();
           cur->moveTo(oldpos.line, oldpos.column + 1);
       }

       if(e->key() == Qt::Key_Backspace)
       {
           if(m_renderer->selectedArea() == QHexRenderer::HexArea)
               this->movePrevious();

           this->movePrevious();
       }
   }
   else if(e->key() == Qt::Key_Insert)
       cur->switchInsertionMode();
   else
       return false;

   return true;
}

bool QHexView::processMove(QHexCursor *cur, QKeyEvent *e)
{
    if(e->matches(QKeySequence::MoveToNextChar) || e->matches(QKeySequence::SelectNextChar))
        this->moveNext(e->matches(QKeySequence::SelectNextChar));
    else if(e->matches(QKeySequence::MoveToPreviousChar) || e->matches(QKeySequence::SelectPreviousChar))
        this->movePrevious(e->matches(QKeySequence::SelectPreviousChar));
    else if(e->matches(QKeySequence::MoveToNextLine) || e->matches(QKeySequence::SelectNextLine))
    {
        if(m_renderer->documentLastLine() == cur->currentLine())
            return true;

        int nextline = cur->currentLine() + 1;

        if(e->matches(QKeySequence::MoveToNextLine))
            cur->moveTo(nextline, cur->currentColumn());
        else
            cur->select(nextline, cur->currentColumn());
    }
    else if(e->matches(QKeySequence::MoveToPreviousLine) || e->matches(QKeySequence::SelectPreviousLine))
    {
        if(!cur->currentLine())
            return true;

        int prevline = cur->currentLine() - 1;

        if(e->matches(QKeySequence::MoveToPreviousLine))
            cur->moveTo(prevline, cur->currentColumn());
        else
            cur->select(prevline, cur->currentColumn());
    }
    else if(e->matches(QKeySequence::MoveToNextPage) || e->matches(QKeySequence::SelectNextPage))
    {
        if(m_renderer->documentLastLine() == cur->currentLine())
            return true;

        int pageline = std::min(m_renderer->documentLastLine(), this->firstVisibleLine() + this->visibleLines());

        if(e->matches(QKeySequence::MoveToNextPage))
            cur->moveTo(pageline, cur->currentColumn());
        else
            cur->select(pageline, cur->currentColumn());
    }
    else if(e->matches(QKeySequence::MoveToPreviousPage) || e->matches(QKeySequence::SelectPreviousPage))
    {
        if(!cur->currentLine())
            return true;

        int pageline = 0;

        if(this->firstVisibleLine() > this->visibleLines())
            pageline = std::max(0, this->firstVisibleLine() - this->visibleLines());

        if(e->matches(QKeySequence::MoveToPreviousPage))
            cur->moveTo(pageline, cur->currentColumn());
        else
            cur->select(pageline, cur->currentColumn());
    }
    else if(e->matches(QKeySequence::MoveToStartOfDocument) || e->matches(QKeySequence::SelectStartOfDocument))
    {
        if(!cur->currentLine())
            return true;

        if(e->matches(QKeySequence::MoveToStartOfDocument))
            cur->moveTo(0, 0);
        else
            cur->select(0, 0);
    }
    else if(e->matches(QKeySequence::MoveToEndOfDocument) || e->matches(QKeySequence::SelectEndOfDocument))
    {
        if(m_renderer->documentLastLine() == cur->currentLine())
            return true;

        if(e->matches(QKeySequence::MoveToEndOfDocument))
            cur->moveTo(m_renderer->documentLastLine(), m_renderer->documentLastColumn());
        else
            cur->select(m_renderer->documentLastLine(), m_renderer->documentLastColumn());
    }
    else if(e->matches(QKeySequence::MoveToStartOfLine) || e->matches(QKeySequence::SelectStartOfLine))
    {
        if(e->matches(QKeySequence::MoveToStartOfLine))
            cur->moveTo(cur->currentLine(), 0);
        else
            cur->select(cur->currentLine(), 0);
    }
    else if(e->matches(QKeySequence::MoveToEndOfLine) || e->matches(QKeySequence::SelectEndOfLine))
    {
        if(e->matches(QKeySequence::MoveToEndOfLine))
        {
            if(cur->currentLine() == m_renderer->documentLastLine())
                cur->moveTo(cur->currentLine(), m_renderer->documentLastColumn());
            else
                cur->moveTo(cur->currentLine(), HEX_LINE_LAST_COLUMN, 0);
        }
        else
        {
            if(cur->currentLine() == m_renderer->documentLastLine())
                cur->select(cur->currentLine(), m_renderer->documentLastColumn());
            else
                cur->select(cur->currentLine(), HEX_LINE_LAST_COLUMN, 0);
        }
    }
    else
        return false;

    return true;
}

bool QHexView::processTextInput(QHexCursor *cur, QKeyEvent *e)
{
    if(m_readonly || (e->modifiers() & Qt::ControlModifier))
        return false;

    uchar key = static_cast<uchar>(e->text()[0].toLatin1());

    if((m_renderer->selectedArea() == QHexRenderer::HexArea))
    {
        if(!((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f'))) // Check if is a Hex Char
            return false;

        uchar val = static_cast<uchar>(QString(static_cast<char>(key)).toUInt(nullptr, 16));
        m_document->removeSelection();

        if(m_document->atEnd() || (cur->currentNibble() && (cur->insertionMode() == QHexCursor::InsertMode)))
        {
            m_document->insert(cur->position().offset(), val << 4); // X0 byte
            this->moveNext();
            return true;
        }

        uchar ch = static_cast<uchar>(m_document->at(cur->position().offset()));

        if(cur->currentNibble()) // X0
            val = (ch & 0x0F) | (val << 4);
        else // 0X
            val = (ch & 0xF0) | val;

        m_document->replace(cur->position().offset(), val);
        this->moveNext();
        return true;
    }

    if((m_renderer->selectedArea() == QHexRenderer::AsciiArea))
    {
        if(!(key >= 0x20 && key <= 0x7E)) // Check if is a Printable Char
            return false;

        m_document->removeSelection();

        if(!m_document->atEnd() && (cur->insertionMode() == QHexCursor::OverwriteMode))
            m_document->replace(cur->position().offset(), key);
        else
            m_document->insert(cur->position().offset(), key);

        QKeyEvent keyevent(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        qApp->sendEvent(this, &keyevent);
        return true;
    }

    return false;
}

void QHexView::adjustScrollBars()
{
    QScrollBar *vscrollbar = this->verticalScrollBar();
    QScrollBar *hscrollbar = this->horizontalScrollBar();

    if(m_renderer->documentLines() > this->visibleLines())
        this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    else
        this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    if(m_renderer->documentWidth() > this->width())
        this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    else
        this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    hscrollbar->setMaximum(m_renderer->documentWidth());

    if(m_renderer->documentLines() <= this->visibleLines())
        vscrollbar->setMaximum(m_renderer->documentLines());
    else
        vscrollbar->setMaximum(m_renderer->documentLines() - this->visibleLines() + 1);
}

void QHexView::renderLine(int line)
{
    if(!this->isLineVisible(line))
        return;

    this->viewport()->update(m_renderer->getLineRect(line, this->firstVisibleLine()));
}

int QHexView::firstVisibleLine() const { return this->verticalScrollBar()->value(); }
int QHexView::lastVisibleLine() const { return this->firstVisibleLine() + this->visibleLines() - 1; }

int QHexView::visibleLines() const
{
    QFontMetrics fontmetrics = this->fontMetrics();
    int vl = std::ceil(this->height() / fontmetrics.height());

    return std::min(vl, m_renderer->documentLines());
}

bool QHexView::isLineVisible(int line) const
{
    if(line < this->firstVisibleLine())
        return false;

    if(line > this->lastVisibleLine())
        return false;

    return true;
}

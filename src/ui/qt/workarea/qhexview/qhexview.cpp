#include "qhexview.h"
#include "document/buffer/qmemorybuffer.h"
#include "QtVGMRoot.h"
#include <QFontDatabase>
#include <QApplication>
#include <QPaintEvent>
#include <QHelpEvent>
#include <QScrollBar>
#include <QPainter>
#include <QToolTip>
#include <QTextFormat>
#include <QWidget>
#include <cmath>
#include <cctype>

#define CURSOR_BLINK_INTERVAL 500  // ms
#define DOCUMENT_WHEEL_LINES 3
#define HEX_UNPRINTABLE_CHAR '.'

QHexView::QHexView(QWidget *parent)
    : QAbstractScrollArea(parent), m_document(nullptr), m_readonly(false), m_cursor(nullptr),
      selected_item(nullptr) {
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  if (f.styleHint() != QFont::TypeWriter) {
    f.setFamily("Monospace");  // Force Monospaced font
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
}

void QHexView::setDocument(QHexDocument *document) {
  if (m_document)
    m_document->deleteLater();
  m_document = document;

  if (m_cursor)
    m_cursor->deleteLater();
  m_cursor = new QHexCursor(this);

  m_selectedarea = QHexView::HEX_AREA;
  m_cursorenabled = false;

  connect(m_document, &QHexDocument::documentChanged, this, [&]() {
    this->adjustScrollBars();
    this->viewport()->update();
  });

  connect(this->cursor(), &QHexCursor::positionChanged, this, &QHexView::moveToSelection);
  connect(this->cursor(), &QHexCursor::insertionModeChanged, this, &QHexView::renderCurrentLine);

  this->adjustScrollBars();
  this->viewport()->update();
}

void QHexView::setReadOnly(bool b) {
  m_readonly = b;
  if (m_cursor)
    cursor()->setInsertionMode(QHexCursor::OverwriteMode);
}

void QHexView::setHexLineWidth(qint8 width) {
  m_linewidth = width;
  if (m_document)
    m_document->setHexLineWidth(width);
  if (m_cursor)
    m_cursor->setLineWidth(width);
}

/**
 * Sets the highlighted VGM item
 * Pass nullptr to clear selection
 * The item will be scrolled to if it is not on screen
 */
void QHexView::setSelectedItem(VGMItem *item) {
  selected_item = item;
  if (selected_item) {
    auto selected_line =
        (selected_item->dwOffset - document()->vgmFile()->dwOffset) / hexLineWidth();

    if (!isLineVisible(selected_line)) {
      QScrollBar *vscrollbar = this->verticalScrollBar();
      int scrollPos = static_cast<int>(
          std::max(quint64(0), selected_line - this->visibleLines() / 2) / documentSizeFactor());
      vscrollbar->setValue(scrollPos);
      return;  // don't need to update viewport again
    }
  }
  this->viewport()->update();
}

bool QHexView::event(QEvent *e) {
  if (m_document && (e->type() == QEvent::ToolTip)) {
    QHelpEvent *helpevent = static_cast<QHelpEvent *>(e);
    QHexPosition position;

    QPoint abspos = absolutePosition(helpevent->pos());
    if (this->hitTest(abspos, &position, this->firstVisibleLine())) {
      QString comments = m_document->metadata()->comments(position.line, position.column);

      if (!comments.isEmpty())
        QToolTip::showText(helpevent->globalPos(), comments, this);
    }

    return true;
  }

  return QAbstractScrollArea::event(e);
}

void QHexView::changeEvent(QEvent *e) {
  bool font_change = e->type() == QEvent::FontChange;
  QAbstractScrollArea::changeEvent(e);
  if (font_change)
    this->adjustScrollBars();
}

void QHexView::keyPressEvent(QKeyEvent *e) {
  if (!m_document) {
    QAbstractScrollArea::keyPressEvent(e);
    return;
  }

  QHexCursor *cur = this->cursor();

  m_blinktimer->stop();
  this->enableCursor();

  bool handled = this->processMove(cur, e);
  if (!handled)
    handled = this->processAction(cur, e);
  if (!handled)
    handled = this->processTextInput(cur, e);
  if (!handled)
    QAbstractScrollArea::keyPressEvent(e);

  m_blinktimer->start();
}

QPoint QHexView::absolutePosition(const QPoint &pos) const {
  QPoint shift(horizontalScrollBar()->value(), 0);
  return pos + shift;
}

void QHexView::mousePressEvent(QMouseEvent *e) {
  QAbstractScrollArea::mousePressEvent(e);

  if ((e->buttons() != Qt::LeftButton))
    return;

  QHexPosition position;
  QPoint abspos = absolutePosition(e->pos());

  if (!this->hitTest(abspos, &position, this->firstVisibleLine()))
    return;

  this->selectArea(abspos);

  if (editableArea(this->selectedArea()))
    cursor()->moveTo(position);

  e->accept();
}

void QHexView::mouseMoveEvent(QMouseEvent *e) {
  QAbstractScrollArea::mouseMoveEvent(e);
  if (!m_document)
    return;

  QPoint abspos = absolutePosition(e->pos());

  if (e->buttons() == Qt::LeftButton) {
    if (m_blinktimer->isActive()) {
      m_blinktimer->stop();
      this->enableCursor(false);
    }

    QHexPosition position;

    if (!this->hitTest(abspos, &position, this->firstVisibleLine()))
      return;

    cursor()->select(position.line, position.column, 0);
    e->accept();
  }

  if (e->buttons() != Qt::NoButton)
    return;

  auto hittest = this->hitTestArea(abspos);

  if (editableArea(hittest))
    this->setCursor(Qt::IBeamCursor);
  else
    this->setCursor(Qt::ArrowCursor);
}

void QHexView::mouseReleaseEvent(QMouseEvent *e) {
  QAbstractScrollArea::mouseReleaseEvent(e);
  if (e->button() != Qt::LeftButton)
    return;
  if (!m_blinktimer->isActive())
    m_blinktimer->start();
  e->accept();
}

void QHexView::focusInEvent(QFocusEvent *e) {
  QAbstractScrollArea::focusInEvent(e);

  this->enableCursor();
  m_blinktimer->start();
}

void QHexView::focusOutEvent(QFocusEvent *e) {
  QAbstractScrollArea::focusOutEvent(e);

  m_blinktimer->stop();
  this->enableCursor(false);
}

void QHexView::wheelEvent(QWheelEvent *e) {
  if (e->angleDelta().y() == 0) {
    int value = this->verticalScrollBar()->value();

    if (e->angleDelta().x() < 0)  // Scroll Down
      this->verticalScrollBar()->setValue(value + DOCUMENT_WHEEL_LINES);
    else if (e->angleDelta().x() > 0)  // Scroll Up
      this->verticalScrollBar()->setValue(value - DOCUMENT_WHEEL_LINES);

    return;
  }

  QAbstractScrollArea::wheelEvent(e);
}

void QHexView::resizeEvent(QResizeEvent *e) {
  QAbstractScrollArea::resizeEvent(e);
  this->adjustScrollBars();
}

void QHexView::paintEvent(QPaintEvent *e) {
  if (!m_document)
    return;

  QPainter painter(this->viewport());
  painter.setFont(this->font());

  const QRect &paint_zone = e->rect();
  const quint64 firstVisible = this->firstVisibleLine();
  const int lineHeight = this->lineHeight();
  const int headerCount = this->headerLineCount();

  // these are lines from the point of view of the visible rect
  // where the first "headerCount" are taken by the header
  const int first = (paint_zone.top() / lineHeight);               // included
  const int lastPlusOne = (paint_zone.bottom() / lineHeight) + 1;  // excluded

  // compute document lines, adding firstVisible and removing the header
  // the max is necessary if the rect covers the header
  const quint64 begin = firstVisible + std::max(first - headerCount, 0);
  const quint64 end = firstVisible + std::max(lastPlusOne - headerCount, 0);

  painter.save();
  painter.translate(-this->horizontalScrollBar()->value(), 0);
  this->render(&painter, begin, end, firstVisible);
  this->renderFrame(&painter);
  painter.restore();
}

void QHexView::moveToSelection() {
  QHexCursor *cur = cursor();

  if (!this->isLineVisible(cur->currentLine())) {
    QScrollBar *vscrollbar = this->verticalScrollBar();
    int scrollPos =
        static_cast<int>(std::max(quint64(0), (cur->currentLine() - this->visibleLines() / 2)) /
                         documentSizeFactor());
    vscrollbar->setValue(scrollPos);
  } else
    this->viewport()->update();
}

void QHexView::blinkCursor() {
  m_cursorenabled = !m_cursorenabled;
  this->renderCurrentLine();
}

void QHexView::moveNext(bool select) {
  QHexCursor *cur = cursor();
  quint64 line = cur->currentLine();
  int column = cur->currentColumn();
  bool lastcell = (line >= this->documentLastLine()) && (column >= this->documentLastColumn());

  if ((this->selectedArea() == QHexView::ASCII_AREA) && lastcell)
    return;

  int nibbleindex = cur->currentNibble();

  if (this->selectedArea() == QHexView::HEX_AREA) {
    if (lastcell && !nibbleindex)
      return;

    if (cur->position().offset() >= m_document->length() && nibbleindex)
      return;
  }

  if ((this->selectedArea() == QHexView::HEX_AREA)) {
    nibbleindex++;
    nibbleindex %= 2;

    if (nibbleindex)
      column++;
  } else {
    nibbleindex = 1;
    column++;
  }

  if (column > this->hexLineWidth() - 1) {
    line = std::min(this->documentLastLine(), line + 1);
    column = 0;
    nibbleindex = 1;
  }

  if (select)
    cur->select(line, std::min(this->hexLineWidth() - 1, column), nibbleindex);
  else
    cur->moveTo(line, std::min(this->hexLineWidth() - 1, column), nibbleindex);
}

void QHexView::movePrevious(bool select) {
  QHexCursor *cur = cursor();
  quint64 line = cur->currentLine();
  int column = cur->currentColumn();
  bool firstcell = !line && !column;

  if ((this->selectedArea() == QHexView::ASCII_AREA) && firstcell)
    return;

  int nibbleindex = cur->currentNibble();

  if ((this->selectedArea() == QHexView::HEX_AREA) && firstcell && nibbleindex)
    return;

  if ((this->selectedArea() == QHexView::HEX_AREA)) {
    nibbleindex--;
    nibbleindex %= 2;
    if (!nibbleindex)
      column--;
  } else {
    nibbleindex = 1;
    column--;
  }

  if (column < 0) {
    line = std::max(quint64(0), line - 1);
    column = this->hexLineWidth() - 1;
    nibbleindex = 0;
  }

  if (select)
    cur->select(line, std::max(0, column), nibbleindex);
  else
    cur->moveTo(line, std::max(0, column), nibbleindex);
}

void QHexView::renderCurrentLine() {
  if (m_document)
    this->renderLine(cursor()->currentLine());
}

bool QHexView::processAction(QHexCursor *cur, QKeyEvent *e) {
  if (m_readonly)
    return false;

  if (e->modifiers() != Qt::NoModifier) {
    if (e->matches(QKeySequence::SelectAll)) {
      cursor()->moveTo(0, 0);
      cursor()->select(this->documentLastLine(), this->documentLastColumn() - 1);
    } else if (e->matches(QKeySequence::Undo))
      m_document->undo();
    else if (e->matches(QKeySequence::Redo))
      m_document->redo();
    else if (e->matches(QKeySequence::Cut))
      m_document->cut(cursor(), (this->selectedArea() == QHexView::HEX_AREA));
    else if (e->matches(QKeySequence::Copy))
      m_document->copy(cursor(), (this->selectedArea() == QHexView::HEX_AREA));
    else if (e->matches(QKeySequence::Paste))
      m_document->paste(cursor(), (this->selectedArea() == QHexView::HEX_AREA));
    else
      return false;

    return true;
  }

  if ((e->key() == Qt::Key_Backspace) || (e->key() == Qt::Key_Delete)) {
    if (!cur->hasSelection()) {
      const QHexPosition &pos = cur->position();

      if (pos.offset() <= 0)
        return true;

      if (e->key() == Qt::Key_Backspace)
        m_document->remove(cur->position().offset() - 1, 1);
      else
        m_document->remove(cur->position().offset(), 1);
    } else {
      QHexPosition oldpos = cur->selectionStart();
      m_document->removeSelection(cursor());
      cur->moveTo(oldpos.line, oldpos.column + 1);
    }

    if (e->key() == Qt::Key_Backspace) {
      if (this->selectedArea() == QHexView::HEX_AREA)
        this->movePrevious();

      this->movePrevious();
    }
  } else if (e->key() == Qt::Key_Insert)
    cur->switchInsertionMode();
  else
    return false;

  return true;
}

bool QHexView::processMove(QHexCursor *cursor, QKeyEvent *e) {
  if (e->matches(QKeySequence::MoveToNextChar)) {
    this->moveNext(false);

  } else if (e->matches(QKeySequence::SelectNextChar)) {
    this->moveNext(true);

  } else if (e->matches(QKeySequence::MoveToPreviousChar)) {
    this->movePrevious(false);

  } else if (e->matches(QKeySequence::SelectPreviousChar)) {
    this->movePrevious(true);

  } else if (e->matches(QKeySequence::MoveToNextLine) || e->matches(QKeySequence::SelectNextLine)) {
    if (this->documentLastLine() == cursor->currentLine())
      return true;
    int nextline = cursor->currentLine() + 1;
    if (e->matches(QKeySequence::MoveToNextLine))
      cursor->moveTo(nextline, cursor->currentColumn());
    else
      cursor->select(nextline, cursor->currentColumn());

  } else if (e->matches(QKeySequence::MoveToPreviousLine) ||
             e->matches(QKeySequence::SelectPreviousLine)) {
    if (!cursor->currentLine())
      return true;
    quint64 prevline = cursor->currentLine() - 1;
    if (e->matches(QKeySequence::MoveToPreviousLine))
      cursor->moveTo(prevline, cursor->currentColumn());
    else
      cursor->select(prevline, cursor->currentColumn());

  } else if (e->matches(QKeySequence::MoveToNextPage) || e->matches(QKeySequence::SelectNextPage)) {
    if (this->documentLastLine() == cursor->currentLine())
      return true;
    int pageline = std::min(this->documentLastLine(), cursor->currentLine() + this->visibleLines());
    if (e->matches(QKeySequence::MoveToNextPage))
      cursor->moveTo(pageline, cursor->currentColumn());
    else
      cursor->select(pageline, cursor->currentColumn());

  } else if (e->matches(QKeySequence::MoveToPreviousPage) ||
             e->matches(QKeySequence::SelectPreviousPage)) {
    if (!cursor->currentLine())
      return true;
    quint64 pageline = std::max(quint64(0), cursor->currentLine() - this->visibleLines());
    if (e->matches(QKeySequence::MoveToPreviousPage))
      cursor->moveTo(pageline, cursor->currentColumn());
    else
      cursor->select(pageline, cursor->currentColumn());

  } else if (e->matches(QKeySequence::MoveToStartOfDocument) ||
             e->matches(QKeySequence::SelectStartOfDocument)) {
    if (!cursor->currentLine())
      return true;
    if (e->matches(QKeySequence::MoveToStartOfDocument))
      cursor->moveTo(0, 0);
    else
      cursor->select(0, 0);

  } else if (e->matches(QKeySequence::MoveToEndOfDocument) ||
             e->matches(QKeySequence::SelectEndOfDocument)) {
    if (this->documentLastLine() == cursor->currentLine())
      return true;

    if (e->matches(QKeySequence::MoveToEndOfDocument))
      cursor->moveTo(this->documentLastLine(), this->documentLastColumn());
    else
      cursor->select(this->documentLastLine(), this->documentLastColumn());

  } else if (e->matches(QKeySequence::MoveToStartOfLine)) {
    cursor->moveTo(cursor->currentLine(), 0);

  } else if (e->matches(QKeySequence::SelectStartOfLine)) {
    cursor->select(cursor->currentLine(), 0);

  } else if (e->matches(QKeySequence::MoveToEndOfLine)) {
    if (cursor->currentLine() == this->documentLastLine())
      cursor->moveTo(cursor->currentLine(), this->documentLastColumn());
    else
      cursor->moveTo(cursor->currentLine(), this->hexLineWidth() - 1, 0);

  } else if (e->matches(QKeySequence::SelectEndOfLine)) {
    if (cursor->currentLine() == this->documentLastLine())
      cursor->select(cursor->currentLine(), this->documentLastColumn());
    else
      cursor->select(cursor->currentLine(), this->hexLineWidth() - 1, 0);
  } else {
    return false;
  }

  return true;
}

bool QHexView::processTextInput(QHexCursor *cur, QKeyEvent *e) {
  if (m_readonly || (e->modifiers() & Qt::ControlModifier))
    return false;

  uchar key = static_cast<uchar>(e->text()[0].toLatin1());

  if ((this->selectedArea() == QHexView::HEX_AREA)) {
    if (!((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f')))  // Check if is a Hex Char
      return false;

    uchar val = static_cast<uchar>(QString(static_cast<char>(key)).toUInt(nullptr, 16));
    m_document->removeSelection(cur);

    if (m_document->atEnd(cur) ||
        (cur->currentNibble() && (cur->insertionMode() == QHexCursor::InsertMode))) {
      m_document->insert(cur->position().offset(), val << 4);  // X0 byte
      this->moveNext();
      return true;
    }

    uchar ch = static_cast<uchar>(m_document->at(cur->position().offset()));

    if (cur->currentNibble())  // X0
      val = (ch & 0x0F) | (val << 4);
    else  // 0X
      val = (ch & 0xF0) | val;

    m_document->replace(cur->position().offset(), val);
    this->moveNext();
    return true;
  }

  if ((this->selectedArea() == QHexView::ASCII_AREA)) {
    if (!(key >= 0x20 && key <= 0x7E))  // Check if is a Printable Char
      return false;

    m_document->removeSelection(cur);

    if (!m_document->atEnd(cur) && (cur->insertionMode() == QHexCursor::OverwriteMode))
      m_document->replace(cur->position().offset(), key);
    else
      m_document->insert(cur->position().offset(), key);

    QKeyEvent keyevent(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    qApp->sendEvent(this, &keyevent);
    return true;
  }

  return false;
}

void QHexView::adjustScrollBars() {
  if (!m_document)
    return;

  QScrollBar *vscrollbar = this->verticalScrollBar();
  int sizeFactor = this->documentSizeFactor();
  vscrollbar->setSingleStep(sizeFactor);

  quint64 docLines = this->documentLines();
  quint64 visLines = this->visibleLines();

  if (docLines > visLines) {
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    vscrollbar->setMaximum(static_cast<int>((docLines - visLines) / sizeFactor + 1));
  } else {
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vscrollbar->setValue(0);
    vscrollbar->setMaximum(static_cast<int>(docLines));
  }

  QScrollBar *hscrollbar = this->horizontalScrollBar();
  int documentWidth = this->documentWidth();
  int viewportWidth = viewport()->width();

  if (documentWidth > viewportWidth) {
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    // +1 to see the rightmost vertical line, +2 seems more pleasant to the eyes
    hscrollbar->setMaximum(documentWidth - viewportWidth + 2);
  } else {
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    hscrollbar->setValue(0);
    hscrollbar->setMaximum(documentWidth);
  }
}

void QHexView::renderLine(quint64 line) {
  if (!this->isLineVisible(line))
    return;
  this->viewport()->update(/*this->getLineRect(line, this->firstVisibleLine())*/);
}

quint64 QHexView::firstVisibleLine() const {
  return static_cast<quint64>(this->verticalScrollBar()->value()) * documentSizeFactor();
}
quint64 QHexView::lastVisibleLine() const {
  return this->firstVisibleLine() + this->visibleLines() - 1;
}

quint64 QHexView::visibleLines() const {
  int visLines = std::ceil(this->height() / this->lineHeight()) - this->headerLineCount();
  return std::min(quint64(visLines), this->documentLines());
}

bool QHexView::isLineVisible(quint64 line) const {
  if (!m_document)
    return false;
  if (line < this->firstVisibleLine())
    return false;
  if (line > this->lastVisibleLine())
    return false;
  return true;
}

int QHexView::documentSizeFactor() const {
  int factor = 1;

  if (m_document) {
    quint64 docLines = this->documentLines();
    if (docLines >= INT_MAX)
      factor = static_cast<int>(docLines / INT_MAX) + 1;
  }

  return factor;
}

void QHexView::renderFrame(QPainter *painter) {
  QRect rect = painter->window();
  int hexx = this->getHexColumnX();
  int asciix = this->getAsciiColumnX();
  int endx = this->getEndColumnX();

  // x coordinates are in absolute space
  // y coordinates are in viewport space
  // see QHexView::paintEvent where the painter has been shifted horizontally

  painter->drawLine(0, this->headerLineCount() * this->lineHeight() - 1, endx,
                    this->headerLineCount() * this->lineHeight() - 1);

  painter->drawLine(hexx, rect.top(), hexx, rect.bottom());

  painter->drawLine(asciix, rect.top(), asciix, rect.bottom());

  painter->drawLine(endx, rect.top(), endx, rect.bottom());
}

void QHexView::render(QPainter *painter, quint64 begin, quint64 end, quint64 firstline) {
  QPalette palette = qApp->palette();

  this->drawHeader(painter, palette);

  quint64 documentLines = this->documentLines();
  for (quint64 line = begin; line < std::min(end, documentLines); line++) {
    QRect linerect = this->getLineRect(line, firstline);
    if (line % 2)  // alternate background color for each line
      painter->fillRect(linerect, palette.brush(QPalette::Window));
    else
      painter->fillRect(linerect, palette.brush(QPalette::Base));

    this->drawAddress(painter, palette, linerect, line);
    this->drawHex(painter, palette, linerect, line);
    this->drawAscii(painter, palette, linerect, line);
  }
}

void QHexView::enableCursor(bool b) {
  m_cursorenabled = b;
}

void QHexView::selectArea(const QPoint &pt) {
  auto area = this->hitTestArea(pt);
  if (!editableArea(area))
    return;
  m_selectedarea = area;
}

bool QHexView::hitTest(const QPoint &pt, QHexPosition *position, quint64 firstline) const {
  auto area = this->hitTestArea(pt);
  if (!editableArea(area))
    return false;

  position->line = std::min(firstline + (pt.y() / this->lineHeight()) - headerLineCount(),
                            this->documentLastLine());
  position->lineWidth = this->hexLineWidth();

  if (area == QHexView::HEX_AREA) {
    int relx = pt.x() - this->getHexColumnX() - this->borderSize();
    // divide relx by cellWidth to get the column, expanded to account for rounding
    int column = relx * this->hexLineWidth() * 3 / this->getNCellsWidth(this->hexLineWidth() * 3);
    position->column = column / 3;
    // first char is nibble 1, 2nd and space are 0
    position->nibbleindex = (column % 3 == 0) ? 1 : 0;
  } else {
    int relx = pt.x() - this->getAsciiColumnX() - this->borderSize();
    // divide relx by cellWidth to get the column, expanded to account for rounding
    position->column = relx * this->hexLineWidth() / this->getNCellsWidth(this->hexLineWidth());
    position->nibbleindex = 1;
  }

  if (position->line == this->documentLastLine()) {
    QByteArray ba = this->getLine(position->line);  // Check last line's columns
    position->column = std::min(position->column, static_cast<int>(ba.length()));
  } else {
    position->column = std::min(position->column, hexLineWidth() - 1);
  }

  return true;
}

// Check what RenderArea the point is within
QHexView::RenderArea QHexView::hitTestArea(const QPoint &pt) const {
  if (pt.y() < headerLineCount() * lineHeight())
    return QHexView::HEADER_AREA;

  if ((pt.x() >= this->borderSize()) && (pt.x() <= (this->getHexColumnX() - this->borderSize())))
    return QHexView::ADDRESS_AREA;

  if ((pt.x() > (this->getHexColumnX() + this->borderSize())) &&
      (pt.x() < (this->getAsciiColumnX() - this->borderSize())))
    return QHexView::HEX_AREA;

  if ((pt.x() > (this->getAsciiColumnX() + this->borderSize())) &&
      (pt.x() < (this->getEndColumnX() - this->borderSize())))
    return QHexView::ASCII_AREA;

  return QHexView::EXTRA_AREA;
}

QHexView::RenderArea QHexView::selectedArea() const {
  return m_selectedarea;
}

quint64 QHexView::documentLastLine() const {
  return this->documentLines() - 1;
}
int QHexView::documentLastColumn() const {
  return this->getLine(this->documentLastLine()).length();
}
quint64 QHexView::documentLines() const {
  return std::ceil(this->rendererLength() / static_cast<float>(hexLineWidth()));
}
// Physical height of each line, based on the current font
int QHexView::lineHeight() const {
  return this->fontMetrics().height();
}

int QHexView::documentWidth() const {
  return this->getEndColumnX();
}

QRect QHexView::getLineRect(quint64 line, quint64 firstline) const {
  return QRect(0, static_cast<int>((line - firstline + headerLineCount()) * lineHeight()),
               this->getEndColumnX(), lineHeight());
}

int QHexView::borderSize() const {
  if (m_document)
    return this->getNCellsWidth(m_document->areaIndent());
  return this->getNCellsWidth(DEFAULT_AREA_IDENTATION);
}

int QHexView::hexLineWidth() const {
  if (m_document)
    return m_document->hexLineWidth();
  return DEFAULT_HEX_LINE_LENGTH;
}

QString QHexView::hexString(quint64 line, QByteArray *rawline) const {
  QByteArray lrawline = this->getLine(line);
  if (rawline)
    *rawline = lrawline;

  return lrawline.toHex(' ').toUpper() + " ";
}

QString QHexView::asciiString(quint64 line, QByteArray *rawline) const {
  QByteArray lrawline = this->getLine(line);
  if (rawline)
    *rawline = lrawline;

  QByteArray ascii = lrawline;
  this->unprintableChars(ascii);
  return ascii;
}

QByteArray QHexView::getLine(quint64 line) const {
  return m_document->read(line * hexLineWidth(), hexLineWidth());
}
qint64 QHexView::rendererLength() const {
  return m_document->length() + 1;
}

int QHexView::getAddressWidth() const {
  quint64 maxAddr = m_document->baseAddress() + this->rendererLength();
  if (maxAddr <= 0xFFFF)
    return 4;
  if (maxAddr <= 0xFFFFFFFF)
    return 8;

  return QString::number(maxAddr, 16).length();
}

int QHexView::getHexColumnX() const {
  return this->getNCellsWidth(this->getAddressWidth()) + 2 * this->borderSize();
}
int QHexView::getAsciiColumnX() const {
  return this->getHexColumnX() + this->getNCellsWidth(hexLineWidth() * 3) + 2 * this->borderSize();
}
int QHexView::getEndColumnX() const {
  return this->getAsciiColumnX() + this->getNCellsWidth(hexLineWidth()) + 2 * this->borderSize();
}

int QHexView::getCellWidth() const {
  return getNCellsWidth(1);
}

int QHexView::getNCellsWidth(int n) const {
  return this->fontMetrics().horizontalAdvance(QString(n, ' '));
}

void QHexView::unprintableChars(QByteArray &ascii) const {
  for (char &ch : ascii) {
    if (std::isprint(static_cast<unsigned char>(ch)))
      continue;

    ch = HEX_UNPRINTABLE_CHAR;
  }
}

void QHexView::applyDocumentStyles(QPainter *painter, QTextDocument *textdocument) const {
  textdocument->setDocumentMargin(0);
  textdocument->setUndoRedoEnabled(false);
  textdocument->setDefaultFont(painter->font());
}

void QHexView::applyMetadata(QTextCursor &textcursor, quint64 line, int chars_per_data) const {
  QHexMetadata *metadata = m_document->metadata();

  if (!metadata->hasMetadata(line))
    return;

  const QHexLineMetadata &linemetadata = metadata->get(line);

  for (const QHexMetadataItem &mi : linemetadata) {
    QTextCharFormat charformat;
    if (mi.background.isValid())
      charformat.setBackground(mi.background);
    if (mi.foreground.isValid())
      charformat.setForeground(mi.foreground);

    textcursor.setPosition(mi.start * chars_per_data);
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                            (mi.length * chars_per_data) - (chars_per_data > 1 ? 1 : 0));
    textcursor.setCharFormat(charformat);
  }
}

void QHexView::applySelection(QTextCursor &textcursor, quint64 line, int factor) const {
  QHexCursor *cursor = this->cursor();
  if (!cursor->isLineSelected(line))
    return;

  const QHexPosition &startsel = cursor->selectionStart();
  const QHexPosition &endsel = cursor->selectionEnd();

  if (startsel.line == endsel.line) {
    textcursor.setPosition(startsel.column * factor);
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                            ((endsel.column - startsel.column + 1) * factor) - 1);
  } else {
    if (line == startsel.line)
      textcursor.setPosition(startsel.column * factor);
    else
      textcursor.setPosition(0);

    if (line == endsel.line)
      textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                              ((endsel.column + 1) * factor) - 1);
    else
      textcursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
  }

  QPalette palette = qApp->palette();

  QTextCharFormat charformat;
  charformat.setBackground(palette.color(QPalette::Highlight));
  charformat.setForeground(palette.color(QPalette::HighlightedText));
  textcursor.setCharFormat(charformat);
}

void QHexView::applyCursorAscii(QTextCursor &textcursor, quint64 line) const {
  QHexCursor *cursor = this->cursor();
  if ((line != cursor->currentLine()) || !m_cursorenabled)
    return;

  textcursor.clearSelection();
  textcursor.setPosition(this->cursor()->currentColumn());
  textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

  QPalette palette = qApp->palette();
  QTextCharFormat charformat;

  if ((cursor->insertionMode() == QHexCursor::OverwriteMode) ||
      (m_selectedarea != QHexView::ASCII_AREA)) {
    charformat.setForeground(palette.color(QPalette::Window));
    if (m_selectedarea == QHexView::ASCII_AREA)
      charformat.setBackground(palette.color(QPalette::WindowText));
    else
      charformat.setBackground(palette.color(QPalette::WindowText).lighter(250));
  } else
    charformat.setUnderlineStyle(QTextCharFormat::UnderlineStyle::SingleUnderline);

  textcursor.setCharFormat(charformat);
}

void QHexView::applyCursorHex(QTextCursor &textcursor, quint64 line) const {
  QHexCursor *cursor = this->cursor();
  if ((line != cursor->currentLine()) || !m_cursorenabled)
    return;

  textcursor.clearSelection();
  textcursor.setPosition(this->cursor()->currentColumn() * 3);

  if ((m_selectedarea == QHexView::HEX_AREA) && !this->cursor()->currentNibble())
    textcursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor);

  textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

  if (m_selectedarea == QHexView::ASCII_AREA)
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

  QPalette palette = qApp->palette();
  QTextCharFormat charformat;

  if ((cursor->insertionMode() == QHexCursor::OverwriteMode) ||
      (m_selectedarea != QHexView::HEX_AREA)) {
    charformat.setForeground(palette.color(QPalette::Window));
    if (m_selectedarea == QHexView::HEX_AREA)
      charformat.setBackground(palette.color(QPalette::WindowText));
    else
      charformat.setBackground(palette.color(QPalette::WindowText).lighter(250));
  } else
    charformat.setUnderlineStyle(QTextCharFormat::UnderlineStyle::SingleUnderline);

  textcursor.setCharFormat(charformat);
}

void QHexView::applyEventSelectionStyle(QTextCursor &textcursor, quint64 line, int factor) const {
  if (!selected_item) {
    return;
  }

  struct {
    uint32_t line, column;
  } startsel, endsel;

  auto file_offset = selected_item->dwOffset - document()->vgmFile()->dwOffset;
  startsel.line = file_offset / hexLineWidth();
  startsel.column = file_offset % hexLineWidth();

  auto selection_end = file_offset + selected_item->unLength - 1;
  endsel.line = selection_end / hexLineWidth();
  endsel.column = selection_end % hexLineWidth();

  if (line < startsel.line || line > endsel.line) {
    return;
  }

  if (startsel.line == endsel.line) {
    textcursor.setPosition(startsel.column * factor);
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                            ((endsel.column - startsel.column + 1) * factor) - 1);
  } else {
    if (line == startsel.line) {
      textcursor.setPosition(startsel.column * factor);
    } else {
      textcursor.setPosition(0);
    }

    if (line == endsel.line) {
      textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                              ((endsel.column + 1) * factor) - 1);
    } else {
      textcursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }
  }

  QTextCharFormat charformat;
  charformat.setFontWeight(QFont::Bold);
  textcursor.mergeCharFormat(charformat);
}

void QHexView::drawAddress(QPainter *painter, const QPalette &palette, const QRect &linerect,
                           quint64 line) {
  quint64 addr = line * hexLineWidth() + m_document->baseAddress();
  QString addrStr =
      QString::number(addr, 16).rightJustified(this->getAddressWidth(), QLatin1Char('0')).toUpper();

  QRect addressrect = linerect;
  addressrect.setWidth(this->getHexColumnX());

  painter->save();
  painter->setPen(palette.color(QPalette::Highlight));
  painter->drawText(addressrect, Qt::AlignHCenter | Qt::AlignVCenter, addrStr);
  painter->restore();
}

void QHexView::drawHex(QPainter *painter, const QPalette &palette, const QRect &linerect,
                       quint64 line) {
  Q_UNUSED(palette)
  QTextDocument textdocument;
  QTextCursor textcursor(&textdocument);
  QByteArray rawline;

  textcursor.insertText(this->hexString(line, &rawline));

  if (line == this->documentLastLine())
    textcursor.insertText(" ");

  QRect hexrect = linerect;
  hexrect.setX(this->getHexColumnX() + this->borderSize());

  this->applyDocumentStyles(painter, &textdocument);
  this->applyMetadata(textcursor, line, 3);
  this->applySelection(textcursor, line, 3);
  this->applyEventSelectionStyle(textcursor, line, 3);
  this->applyCursorHex(textcursor, line);

  painter->save();
  painter->translate(hexrect.topLeft());
  textdocument.drawContents(painter);
  this->drawEventSelectionOutline(painter, line, 3);
  painter->restore();
}

void QHexView::drawAscii(QPainter *painter, const QPalette &palette, const QRect &linerect,
                         quint64 line) {
  Q_UNUSED(palette)
  QTextDocument textdocument;
  QTextCursor textcursor(&textdocument);
  QByteArray rawline;
  textcursor.insertText(this->asciiString(line, &rawline));

  if (line == this->documentLastLine())
    textcursor.insertText(" ");

  QRect asciirect = linerect;
  asciirect.setX(this->getAsciiColumnX() + this->borderSize());

  this->applyDocumentStyles(painter, &textdocument);
  this->applyMetadata(textcursor, line);
  this->applyEventSelectionStyle(textcursor, line);
  this->applySelection(textcursor, line);
  this->applyCursorAscii(textcursor, line);

  painter->save();
  painter->translate(asciirect.topLeft());
  textdocument.drawContents(painter);
  this->drawEventSelectionOutline(painter, line);
  painter->restore();
}

void QHexView::drawHeader(QPainter *painter, const QPalette &palette) {
  QRect rect = QRect(0, 0, this->getEndColumnX(), this->headerLineCount() * this->lineHeight());
  QString hexheader;

  for (quint8 i = 0; i < this->hexLineWidth(); i++)
    hexheader.append(
        QString("%1 ").arg(QString::number(i, 16).rightJustified(2, QChar('0'))).toUpper());

  QRect addressrect = rect;
  addressrect.setWidth(this->getHexColumnX());

  QRect hexrect = rect;
  hexrect.setX(this->getHexColumnX() + this->borderSize());
  hexrect.setWidth(this->getNCellsWidth(hexLineWidth() * 3));

  QRect asciirect = rect;
  asciirect.setX(this->getAsciiColumnX());
  asciirect.setWidth(this->getEndColumnX() - this->getAsciiColumnX());

  painter->save();
  painter->setPen(palette.color(QPalette::Highlight));

  painter->drawText(addressrect, Qt::AlignHCenter | Qt::AlignVCenter, QString("Offset"));
  // align left for maximum consistency with drawHex() which prints from the left.
  // so hex and positions are aligned vertically
  painter->drawText(hexrect, Qt::AlignLeft | Qt::AlignVCenter, hexheader);
  painter->drawText(asciirect, Qt::AlignHCenter | Qt::AlignVCenter, QString("Ascii"));
  painter->restore();
}

void QHexView::drawEventSelectionOutline(QPainter *painter, quint64 line, const int factor) const {
  // copied code from applyEventSeletionStyle, TODO: consider
  if (!selected_item) {
    return;
  }

  struct {
    uint32_t line, column;
  } startsel, endsel;

  auto file_offset = selected_item->dwOffset - document()->vgmFile()->dwOffset;
  startsel.line = file_offset / hexLineWidth();
  startsel.column = file_offset % hexLineWidth();

  auto selection_end = file_offset + selected_item->unLength - 1;
  endsel.line = selection_end / hexLineWidth();
  endsel.column = selection_end % hexLineWidth();

  if (line < startsel.line || line > endsel.line) {
    return;
  }

  painter->save();
  painter->setPen(QPen(QBrush(Qt::red), 2));

  const float pad = factor > 1 ? getCellWidth() / 2.0 : 0;
  const int start = getNCellsWidth(startsel.column * factor) - pad;
  const int end = getNCellsWidth((endsel.column + 1) * factor) - pad;
  const int right = getNCellsWidth(hexLineWidth() * factor) + pad;
  const int left = -pad;
  const int height = lineHeight() - 1;

  QVector<QLine> outline;

  if (startsel.line == endsel.line) {
    // selection only spans a single line, outline with a rectangle
    outline.push_back({start, 0, end, 0});            // top
    outline.push_back({start, height, end, height});  // bottom
    outline.push_back({start, 0, start, height});     // left
    outline.push_back({end, 0, end, height});         // right
  } else {
    if (line == startsel.line) {
      // first of multiple lines
      outline.push_back({start, 0, right, 0});       // top
      outline.push_back({start, 0, start, height});  // left

      if (endsel.line > startsel.line + 1 || end > start) {
        // the selection is contiguous, outline merges together
        outline.push_back({left, height, start, height});  // top of next line
        outline.push_back({right, 0, right, height});      // right
      } else {
        // the selection is disjoint, outline rectangle wraps around
        outline.push_back({start, height, right, height});  // bottom
      }
    } else if (line == endsel.line) {
      // last of multiple lines
      outline.push_back({left, height, end, height});  // bottom
      outline.push_back({end, 0, end, height});        // right

      if (endsel.line > startsel.line + 1 || end > start) {
        // the selection is contiguous, outline merges together
        outline.push_back({end, 0, right, 0});       // bottom of previous line
        outline.push_back({left, 0, left, height});  // left
      } else {
        // the selection is disjoint, outline rectangle wraps around
        outline.push_back({left, 0, end, 0});  // top
      }
    } else {
      // line in the middle of a multi line selection
      outline.push_back({left, 0, left, height});    // left
      outline.push_back({right, 0, right, height});  // right
    }
  }

  painter->drawLines(outline);
  painter->restore();
}

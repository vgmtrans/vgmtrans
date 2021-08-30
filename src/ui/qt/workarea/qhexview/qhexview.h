#ifndef QHEXVIEW_H
#define QHEXVIEW_H

#define QHEXVIEW_VERSION 3.0

#include <QAbstractScrollArea>
#include <QTimer>
#include <QTextDocument>
#include <QTextCursor>
#include "document/qhexdocument.h"

class QHexView : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit QHexView(QWidget* parent = nullptr);
  QHexDocument* document() const { return m_document; };
  void setDocument(QHexDocument* document);
  void setReadOnly(bool b);
  // Set how many bytes should be displayed per line
  void setHexLineWidth(qint8 width);
  // How many bytes are displayed per line
  qint8 getLineWidth() const { return m_linewidth; };
  void setSelectedItem(VGMItem* item);
  QHexCursor* cursor() const { return m_cursor; };

protected:
  virtual bool event(QEvent* e) override;
  virtual void keyPressEvent(QKeyEvent* e) override;
  virtual void mousePressEvent(QMouseEvent* e) override;
  virtual void mouseMoveEvent(QMouseEvent* e) override;
  virtual void mouseReleaseEvent(QMouseEvent* e) override;
  virtual void focusInEvent(QFocusEvent* e) override;
  virtual void focusOutEvent(QFocusEvent* e) override;
  virtual void wheelEvent(QWheelEvent* e) override;
  virtual void resizeEvent(QResizeEvent* e) override;
  virtual void paintEvent(QPaintEvent* e) override;

private slots:
  void renderCurrentLine();
  void moveToSelection();
  void blinkCursor();

private:
  void moveNext(bool select = false);
  void movePrevious(bool select = false);

private:
  bool processMove(QHexCursor* cur, QKeyEvent* e);
  bool processTextInput(QHexCursor* cur, QKeyEvent* e);
  bool processAction(QHexCursor* cur, QKeyEvent* e);
  void adjustScrollBars();
  void renderLine(quint64 line);
  quint64 firstVisibleLine() const;
  quint64 lastVisibleLine() const;
  quint64 visibleLines() const;
  bool isLineVisible(quint64 line) const;
  int documentSizeFactor() const;
  QPoint absolutePosition(const QPoint& pos) const;

private:
  enum RenderArea { HEADER_AREA, ADDRESS_AREA, HEX_AREA, ASCII_AREA, EXTRA_AREA };
  static constexpr bool editableArea(RenderArea area) {
    return (area == HEX_AREA || area == ASCII_AREA);
  };

private:
  QHexCursor* m_cursor;
  VGMItem* selected_item;
  QHexDocument* m_document;
  QTimer* m_blinktimer;
  bool m_readonly;
  RenderArea m_selectedarea;
  // TODO: move this into qhexcursor
  bool m_cursorenabled;
  // how many bytes displayed per line
  // don't set directly or the cursor and document will desync
  qint8 m_linewidth;

private:
  void renderFrame(QPainter* painter);
  // begin included, end excluded
  void render(QPainter* painter, quint64 start, quint64 end, quint64 firstline);
  void enableCursor(bool b = true);
  void selectArea(const QPoint& pt);

  bool hitTest(const QPoint& pt, QHexPosition* position, quint64 firstline) const;
  RenderArea hitTestArea(const QPoint& pt) const;
  RenderArea selectedArea() const;
  quint64 documentLastLine() const;
  int documentLastColumn() const;
  quint64 documentLines() const;
  int documentWidth() const;
  int lineHeight() const;
  QRect getLineRect(quint64 line, quint64 firstline) const;
  int headerLineCount() const { return 1; }
  int borderSize() const;
  int hexLineWidth() const;

  QString hexString(quint64 line, QByteArray* rawline = nullptr) const;
  QString asciiString(quint64 line, QByteArray* rawline = nullptr) const;
  QByteArray getLine(quint64 line) const;
  qint64 rendererLength() const;
  int getAddressWidth() const;
  int getHexColumnX() const;
  int getAsciiColumnX() const;
  int getEndColumnX() const;
  int getCellWidth() const;
  int getNCellsWidth(int n) const;
  void unprintableChars(QByteArray& ascii) const;

private:
  void applyDocumentStyles(QPainter* painter, QTextDocument* textdocument) const;
  void applyBasicStyle(QTextCursor& textcursor, const QByteArray& rawline, int factor = 1) const;
  void applyMetadata(QTextCursor& textcursor, quint64 line, int factor = 1) const;
  void applySelection(QTextCursor& textcursor, quint64 line, int factor = 1) const;
  void applyCursorAscii(QTextCursor& textcursor, quint64 line) const;
  void applyCursorHex(QTextCursor& textcursor, quint64 line) const;
  void applyEventSelectionStyle(QTextCursor& textcursor, quint64 line, int factor = 1) const;
  void drawAddress(QPainter* painter, const QPalette& palette, const QRect& linerect, quint64 line);
  void drawHex(QPainter* painter, const QPalette& palette, const QRect& linerect, quint64 line);
  void drawAscii(QPainter* painter, const QPalette& palette, const QRect& linerect, quint64 line);
  void drawHeader(QPainter* painter, const QPalette& palette);
  void drawEventSelectionOutline(QPainter* painter, quint64 line, const int factor = 1) const;
};

#endif  // QHEXVIEW_H

#ifndef QHEXVIEW_H
#define QHEXVIEW_H

#define QHEXVIEW_VERSION 3.0

#include <QAbstractScrollArea>
#include <QTimer>
#include "document/qhexrenderer.h"
#include "document/qhexdocument.h"

class QHexView : public QAbstractScrollArea
{
    Q_OBJECT

    public:
        explicit QHexView(QWidget *parent = nullptr);
        QHexDocument* document();
        void setDocument(QHexDocument* document);
        void setReadOnly(bool b);

    protected:
        virtual bool event(QEvent* e);
        virtual void keyPressEvent(QKeyEvent *e);
        virtual void mousePressEvent(QMouseEvent* e);
        virtual void mouseMoveEvent(QMouseEvent* e);
        virtual void mouseReleaseEvent(QMouseEvent* e);
        virtual void focusInEvent(QFocusEvent* e);
        virtual void focusOutEvent(QFocusEvent* e);
        virtual void wheelEvent(QWheelEvent* e);
        virtual void resizeEvent(QResizeEvent* e);
        virtual void paintEvent(QPaintEvent* e);

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

        QPoint absolutePosition(const QPoint & pos) const;

    private:
        QHexDocument* m_document;
        QHexRenderer* m_renderer;
        QTimer* m_blinktimer;
        bool m_readonly;
};

#endif // QHEXVIEW_H

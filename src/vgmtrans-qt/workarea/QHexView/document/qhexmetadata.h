#ifndef QHEXMETADATA_H
#define QHEXMETADATA_H

#include <QLinkedList>
#include <QObject>
#include <QColor>
#include <QHash>

struct QHexMetadataItem
{
    int line, start, length;
    QColor foreground, background;
    QString comment;
};

typedef QLinkedList<QHexMetadataItem> QHexLineMetadata;

class QHexMetadata : public QObject
{
    Q_OBJECT

    public:
        explicit QHexMetadata(QObject *parent = nullptr);
        const QHexLineMetadata& get(int line) const;
        QString comments(int line) const;
        bool hasMetadata(int line) const;
        void clear(int line);
        void clear();

    public:
        void metadata(int line, int start, int length, const QColor& fgcolor, const QColor& bgcolor, const QString& comment);
        void color(int line, int start, int length, const QColor& fgcolor, const QColor& bgcolor);
        void foreground(int line, int start, int length, const QColor& fgcolor);
        void background(int line, int start, int length, const QColor& bgcolor);
        void comment(int line, int start, int length, const QString &comment);

    private:
        void setMetadata(const QHexMetadataItem& mi);

    signals:
        void metadataChanged(int line);
        void metadataCleared();

    private:
        QHash<int, QHexLineMetadata> m_metadata;
};

#endif // QHEXMETADATA_H

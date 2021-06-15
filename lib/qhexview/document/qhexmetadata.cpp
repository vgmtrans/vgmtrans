#include "qhexmetadata.h"

QHexMetadata::QHexMetadata(QObject *parent) : QObject(parent) { }

const QHexLineMetadata &QHexMetadata::get(quint64 line) const
{
    auto it = m_metadata.find(line);
    return it.value();
}

QString QHexMetadata::comments(quint64 line, int column) const
{
    if(!this->hasMetadata(line))
        return QString();

    QString s;

    const auto& linemetadata = this->get(line);

    for(auto& mi : linemetadata)
    {
        if(!(mi.start <= column && column < mi.start + mi.length))
            continue;
        if(mi.comment.isEmpty())
            continue;

        if(!s.isEmpty())
            s += "\n";

        s += mi.comment;
    }

    return s;
}

bool QHexMetadata::hasMetadata(quint64 line) const { return m_metadata.contains(line); }

void QHexMetadata::clear(quint64 line)
{
    auto it = m_metadata.find(line);

    if(it == m_metadata.end())
        return;

    m_metadata.erase(it);
    emit metadataChanged(line);
}

void QHexMetadata::clear()
{
    m_absoluteMetadata.clear();
    m_metadata.clear();
    emit metadataCleared();
}

void QHexMetadata::metadata(qint64 begin, qint64 end, const QColor &fgcolor, const QColor &bgcolor, const QString &comment)
{
    m_absoluteMetadata.append({ begin, end, fgcolor, bgcolor, comment});
    setAbsoluteMetadata(m_absoluteMetadata.back());
}

void QHexMetadata::setAbsoluteMetadata(const QHexMetadataAbsoluteItem& mai)
{
    const quint64 firstRow = mai.begin / m_lineWidth;
    const quint64 lastRow = mai.end / m_lineWidth;

    for (quint64 row = firstRow; row <= lastRow; ++row)
    {
        int start, length;
        if (row == firstRow)
        {
            start = mai.begin % m_lineWidth;
        }
        else
        {
            start = 0;
        }
        if (row == lastRow)
        {
            const int lastChar = mai.end % m_lineWidth;
            length = lastChar - start;
        }
        else
        {
            length = m_lineWidth;
        }
        if (length > 0)
        {
            setMetadata({row, start, length, mai.foreground, mai.background, mai.comment});
        }
    }
}

void QHexMetadata::setLineWidth(quint8 width)
{
    if (width != m_lineWidth)
    {
        m_lineWidth = width;
        // clean m_metadata
        m_metadata.clear();
        // and regenerate with new line width size
        for (int i = 0; i < m_absoluteMetadata.size(); ++i)
        {
            setAbsoluteMetadata(m_absoluteMetadata[i]);
        }
    }
}

void QHexMetadata::metadata(quint64 line, int start, int length, const QColor &fgcolor, const QColor &bgcolor, const QString &comment)
{
    const qint64 begin = line * m_lineWidth + start;
    const qint64 end = begin + length;
    // delegate to the new interface
    this->metadata(begin, end, fgcolor, bgcolor, comment);
}

void QHexMetadata::color(quint64 line, int start, int length, const QColor &fgcolor, const QColor &bgcolor)
{
    this->metadata(line, start, length, fgcolor, bgcolor, QString());
}

void QHexMetadata::foreground(quint64 line, int start, int length, const QColor &fgcolor)
{
    this->color(line, start, length, fgcolor, QColor());
}

void QHexMetadata::background(quint64 line, int start, int length, const QColor &bgcolor)
{
    this->color(line, start, length, QColor(), bgcolor);
}

void QHexMetadata::comment(quint64 line, int start, int length, const QString &comment)
{
    this->metadata(line, start, length, QColor(), QColor(), comment);
}

void QHexMetadata::setMetadata(const QHexMetadataItem &mi)
{
    if(!m_metadata.contains(mi.line))
    {
        QHexLineMetadata linemetadata;
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
        linemetadata << mi;
#else
        linemetadata.push_back(mi);
#endif
        m_metadata[mi.line] = linemetadata;
    }
    else
    {
        QHexLineMetadata& linemetadata = m_metadata[mi.line];
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
        linemetadata << mi;
#else
        linemetadata.push_back(mi);
#endif
    }

    emit metadataChanged(mi.line);
}

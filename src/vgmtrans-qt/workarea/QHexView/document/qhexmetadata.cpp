#include "qhexmetadata.h"

QHexMetadata::QHexMetadata(QObject *parent) : QObject(parent) { }

const QHexLineMetadata &QHexMetadata::get(int line) const
{
    auto it = m_metadata.find(line);
    return it.value();
}

QString QHexMetadata::comments(int line) const
{
    if(!this->hasMetadata(line))
        return QString();

    QString s;

    const auto& linemetadata = this->get(line);

    for(auto& mi : linemetadata)
    {
        if(mi.comment.isEmpty())
            continue;

        if(!s.isEmpty())
            s += "\n";

        s += mi.comment;
    }

    return s;
}

bool QHexMetadata::hasMetadata(int line) const { return m_metadata.contains(line); }

void QHexMetadata::clear(int line)
{
    auto it = m_metadata.find(line);

    if(it == m_metadata.end())
        return;

    m_metadata.erase(it);
    emit metadataChanged(line);
}

void QHexMetadata::clear()
{
    m_metadata.clear();
    emit metadataCleared();
}

void QHexMetadata::metadata(int line, int start, int length, const QColor &fgcolor, const QColor &bgcolor, const QString &comment)
{
    this->setMetadata({ line, start, length, fgcolor, bgcolor, comment});
}

void QHexMetadata::color(int line, int start, int length, const QColor &fgcolor, const QColor &bgcolor)
{
    this->metadata(line, start, length, fgcolor, bgcolor, QString());
}

void QHexMetadata::foreground(int line, int start, int length, const QColor &fgcolor)
{
    this->color(line, start, length, fgcolor, QColor());
}

void QHexMetadata::background(int line, int start, int length, const QColor &bgcolor)
{
    this->color(line, start, length, QColor(), bgcolor);
}

void QHexMetadata::comment(int line, int start, int length, const QString &comment)
{
    this->metadata(line, start, length, QColor(), QColor(), comment);
}

void QHexMetadata::setMetadata(const QHexMetadataItem &mi)
{
    if(!m_metadata.contains(mi.line))
    {
        QHexLineMetadata linemetadata;
        linemetadata << mi;
        m_metadata[mi.line] = linemetadata;
    }
    else
    {
        QHexLineMetadata& linemetadata = m_metadata[mi.line];
        linemetadata << mi;
    }

    emit metadataChanged(mi.line);
}

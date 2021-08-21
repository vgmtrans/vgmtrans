#ifndef QHEXDOCUMENT_H
#define QHEXDOCUMENT_H

#include <QUndoStack>
#include <QFile>
#include "qhexmetadata.h"
#include "qhexcursor.h"
#include "VGMFile.h"

class QHexDocument : public QObject {
  Q_OBJECT

public:
  explicit QHexDocument(VGMFile* vgm_file, QObject* parent = nullptr);

public:
  VGMFile* vgmFile() const { return m_vgm_file; };
  bool isEmpty() const;
  bool atEnd(QHexCursor* cursor) const;
  bool canUndo() const;
  bool canRedo() const;
  qint64 length() const;
  quint64 baseAddress() const;
  QHexMetadata* metadata() const;
  int areaIndent() const;
  void setAreaIndent(quint8 value);
  int hexLineWidth() const;
  void setHexLineWidth(quint8 value);
  void removeSelection(QHexCursor* cursor);
  QByteArray selectedBytes(QHexCursor* cursor) const;
  char at(int offset) const;
  void setBaseAddress(quint64 baseaddress);
  void sync();
  QByteArray read(qint64 offset, int len) const;

public slots:
  void undo();
  void redo();
  void cut(QHexCursor* cursor, bool hex = false);
  void copy(QHexCursor* cursor, bool hex = false);
  void paste(QHexCursor* cursor, bool hex = false);
  void insert(qint64 offset, uchar b);
  void replace(qint64 offset, uchar b);
  void insert(qint64 offset, const QByteArray& data);
  void replace(qint64 offset, const QByteArray& data);
  void remove(qint64 offset, int len);
  

signals:
  void canUndoChanged(bool canUndo);
  void canRedoChanged(bool canRedo);
  void documentChanged();
  void lineChanged(quint64 line);

private:
  VGMFile* m_vgm_file;
  QHexMetadata* m_metadata;
  QUndoStack m_undostack;
  quint64 m_baseaddress;
  quint8 m_areaindent;
  // TODO: move this to the qhexview
  quint8 m_hexlinewidth;
};

#endif  // QHEXEDITDATA_H

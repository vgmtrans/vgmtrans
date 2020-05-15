/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <unordered_map>
#include <variant>
#include <QMdiArea>
#include <QMdiSubWindow>

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

class MdiArea : public QMdiArea {
    Q_OBJECT

   public:
    MdiArea(const MdiArea &) = delete;
    MdiArea &operator=(const MdiArea &) = delete;
    MdiArea(MdiArea &&) = delete;
    MdiArea &operator=(MdiArea &&) = delete;

    static MdiArea *Init();
    static MdiArea *Instance();

    void NewView(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
    void RemoveView(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);

   private:
    std::unordered_map<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>, QMdiSubWindow *> m_registered_views;
    MdiArea(QWidget *parent = nullptr);
};

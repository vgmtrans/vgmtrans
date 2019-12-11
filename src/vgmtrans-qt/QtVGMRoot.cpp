/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QStandardPaths>
#include <QFileDialog>
#include <QString>
#include "QtVGMRoot.h"

QtVGMRoot qtVGMRoot;

void QtVGMRoot::UI_SetRootPtr(VGMRoot **theRoot) {
    *theRoot = &qtVGMRoot;
}

void QtVGMRoot::UI_PreExit() {}

void QtVGMRoot::UI_Exit() {}

void QtVGMRoot::UI_AddRawFile(RawFile *newFile) {
    this->UI_AddedRawFile();
}

void QtVGMRoot::UI_CloseRawFile(RawFile *targFile) {
    this->UI_RemovedRawFile();
}

void QtVGMRoot::UI_OnBeginScan() {}

void QtVGMRoot::UI_SetScanInfo() {}

void QtVGMRoot::UI_OnEndScan() {}

void QtVGMRoot::UI_AddVGMFile(VGMFile *theFile) {
    this->UI_AddedVGMFile();
}

void QtVGMRoot::UI_AddVGMSeq(VGMSeq *theSeq) {}

void QtVGMRoot::UI_AddVGMInstrSet(VGMInstrSet *theInstrSet) {}

void QtVGMRoot::UI_AddVGMSampColl(VGMSampColl *theSampColl) {}

void QtVGMRoot::UI_AddVGMMisc(VGMMiscFile *theMiscFile) {}

void QtVGMRoot::UI_AddVGMColl(VGMColl *theColl) {
    this->UI_AddedVGMColl();
}

void QtVGMRoot::UI_RemoveVGMColl(VGMColl *targColl) {
    this->UI_RemovedVGMColl();
}

void QtVGMRoot::UI_BeginRemoveVGMFiles() {}

void QtVGMRoot::UI_EndRemoveVGMFiles() {}

void QtVGMRoot::UI_AddItemSet(VGMFile *file, std::vector<ItemSet> *itemset) {}

std::string QtVGMRoot::UI_GetOpenFilePath(const std::string &suggestedFilename,
                                          const std::string &extension) {
    return QFileDialog::getOpenFileName(
               QApplication::activeWindow(), "Select a file...",
               QStandardPaths::writableLocation(QStandardPaths::MusicLocation), "All files (*)")
        .toStdString();
}

std::string QtVGMRoot::UI_GetSaveFilePath(const std::string &suggestedFilename,
                                          const std::string &extension) {
    return QFileDialog::getSaveFileName(
               QApplication::activeWindow(), "Save file...",
               QStandardPaths::writableLocation(QStandardPaths::MusicLocation), "All files (*)")
        .toStdString();
}

std::string QtVGMRoot::UI_GetSaveDirPath(const std::string &suggestedDir) {
    return QFileDialog::getExistingDirectory(
               QApplication::activeWindow(), "Save file...",
               QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
               QFileDialog::ShowDirsOnly)
        .toStdString();
}

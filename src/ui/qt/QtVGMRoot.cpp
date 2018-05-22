/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QApplication>
#include <QStandardPaths>
#include <QFileDialog>
#include <QString>
#include "QtVGMRoot.h"

QtVGMRoot qtVGMRoot;

void QtVGMRoot::UI_SetRootPtr(VGMRoot** theRoot)
{
    *theRoot = &qtVGMRoot;
}

void QtVGMRoot::UI_PreExit()
{}

void QtVGMRoot::UI_Exit()
{}

void QtVGMRoot::UI_AddRawFile(RawFile* newFile)
{
    this->UI_AddedRawFile();
}

void QtVGMRoot::UI_CloseRawFile(RawFile* targFile)
{
    this->UI_RemovedRawFile();
}

void QtVGMRoot::UI_OnBeginScan()
{}

void QtVGMRoot::UI_SetScanInfo()
{}

void QtVGMRoot::UI_OnEndScan()
{}

void QtVGMRoot::UI_AddVGMFile(VGMFile* theFile)
{
    this->UI_AddedVGMFile();
}

void QtVGMRoot::UI_AddVGMSeq(VGMSeq* theSeq)
{}

void QtVGMRoot::UI_AddVGMInstrSet(VGMInstrSet* theInstrSet)
{}

void QtVGMRoot::UI_AddVGMSampColl(VGMSampColl* theSampColl)
{}

void QtVGMRoot::UI_AddVGMMisc(VGMMiscFile* theMiscFile)
{}

void QtVGMRoot::UI_AddVGMColl(VGMColl* theColl)
{
    this->UI_AddedVGMColl();
}

void QtVGMRoot::UI_RemoveVGMFile(VGMFile* targFile)
{
    this->UI_RemovedVGMFile();
}

void QtVGMRoot::UI_RemoveVGMColl(VGMColl* targColl)
{
    this->UI_RemovedVGMColl();
}

void QtVGMRoot::UI_BeginRemoveVGMFiles()
{}

void QtVGMRoot::UI_EndRemoveVGMFiles()
{}

void QtVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, void* UI_specific)
{}

void QtVGMRoot::UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset)
{}

std::wstring QtVGMRoot::UI_GetOpenFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
  return QFileDialog::getOpenFileName(QApplication::activeWindow(), tr("Select a file..."),
                                      QStandardPaths::writableLocation(QStandardPaths::MusicLocation), tr("All files (*)")).toStdWString();
}

std::wstring QtVGMRoot::UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
  return QFileDialog::getSaveFileName(QApplication::activeWindow(), tr("Save file..."),
                                      QStandardPaths::writableLocation(QStandardPaths::MusicLocation), tr("All files (*)")).toStdWString();
}

std::wstring QtVGMRoot::UI_GetSaveDirPath(const std::wstring& suggestedDir)
{
  return QFileDialog::getExistingDirectory(QApplication::activeWindow(), tr("Save file..."),
                                           QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QFileDialog::ShowDirsOnly).toStdWString();
}
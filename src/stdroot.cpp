/***************************************************************************
 *   Copyright (C) 2006 by Mike   *
 *   mike@linux   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "stdroot.h"

StdRoot stdroot;

StdRoot::StdRoot()
 : VGMRoot()
{
}

StdRoot::~StdRoot()
{
}


void StdRoot::UI_SetRootPtr(VGMRoot** theRoot)
{
	*theRoot = &stdroot;
}
/*

virtual void UI_SetRootPtr(VGMRoot** theRoot) = 0;
virtual void UI_AddRawFile(RawFile* newFile) {}
virtual void UI_CloseRawFile(RawFile* targFile) {}

virtual void UI_AddVGMFile(VGMFile* theFile);
virtual void UI_AddVGMSeq(VGMSeq* theSeq);
virtual void UI_AddVGMInstrSet(VGMInstrSet* theInstrSet);
virtual void UI_RemoveVGMFile(VGMFile* theFile);
*/
void StdRoot::UI_AddRawFile(RawFile* newFile)
{
//	cout << newFile->GetFileName() << endl;
}
/*
void WinVGMRoot::UI_CloseRawFile(RawFile* targFile)
{
	rawFileTreeView.RemoveFile(targFile);
}*/

void StdRoot::UI_AddVGMSeq(VGMSeq* theSeq)
{
	cout << "Found Sequence in " << theSeq->file->GetFileName() << endl;
}

void StdRoot::UI_AddVGMInstrSet(VGMInstrSet* theInstrSet)
{
	cout << "Found Instrument Set in " << theInstrSet->file->GetFileName() << endl;
}
/*
void WinVGMRoot::UI_RemoveVGMFile(VGMFile* targFile)
{
	VGMFilesView.RemoveFile(targFile);
}

void WinVGMRoot::UI_BeginRemoveVGMFiles()
{
	VGMFilesView.ShowWindow(false);
}

void WinVGMRoot::UI_EndRemoveVGMFiles()
{
	VGMFilesView.ShowWindow(true);
}

void WinVGMRoot::UI_AddItem(VGMFile* file, VGMItem* item, VGMItem* parent, const char* itemName)
{
	itemTreeView.AddItem(file, item, parent, itemName);
}

void WinVGMRoot::UI_AddItemSet(VGMFile* file, vector<ItemSet>* itemset)
{
	itemTreeView.AddItemSet(file, itemset);
}
*/

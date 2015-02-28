/*
 ____   _  ____  _____ _____ ______  __  __  _  ____   _
|  _ \ | ||  _ \|  __//  __//_   _/ |  \/  || ||  _ \ | |
| |_| || || |> /|  _| | <__   | |   | |\/| || || |_| || | 
|____/ |_||_|\_\|____\\____/  |_|   |_|  |_||_||____/ |_|			 

////////////////////////////////////////////////////////////////////////
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
 
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
 
Copyright (c) 2002-2004 by Carlos Jiménez de Parga  
All rights reserved.
For any suggestion or failure report, please contact me by:
e-mail: cjimenez@isometrica.net

Note: I would appreciate very much if you could provide a brief description of your project, 
as well as any report on bugs or errors. This will help me improve the code for the benefit of your
application and the other users    
///////////////////////////////////////////////////////////////////////
Version: 2.3b
Module : CCollection.cpp
Purpose: Code implementation for the CCollection class
Created: CJP / 02-08-2002
History: CJP / 20-02-2004 
Date format: Day-month-year
	
	Update: 20/09/2002

	1. Improved class destructors

	2. Check member variables for NULL values before performing an operation
		
	3. Restructured the class system
	
	4. Better method to enumerate and select MIDI ports
	
	5. Adapted the external thread to a pure virtual function

	6. SysEx reception and sending enabled

	7. Added flexible conversion functions

	8. Find out how to activate the Microsoft software synth. 

	9. Added DLS support

  Update: 25/03/2003

	10. Added exception handling

    11. Fixed bugs with channel groups
  
    12. Redesigned class hierarchy
	
    13. DirectMidi wrapped in the directmidi namespace
  
    14. UNICODE/MBCS support for instruments and ports

	15. Added DELAY effect to software synthesizer ports
	
	16. Project released under GNU (General Public License) terms		 

  Update: 03/10/2003	

	17. Redesigned class interfaces

	18. Safer input port termination thread

	19. New CMasterClock class

	20. DLS files can be loaded from resources

	21. DLS instruments note range support

	22. New CSampleInstrument class added to the library

	23. Direct download of samples to wave-table memory
	
	24. .WAV file sample format supported	

	25. New methods added to the output port class

	26. Fixed small bugs

  Update: 20/02/2004

	27. Added new DirectMusic classes for Audio handling

	28. Improved CMidiPort class with internal buffer run-time resizing

	29. 3D audio positioning


*/

#include "pch.h"
#include "CMidiPart.h"

using namespace directmidi;

// Class constructor

CCollection::CCollection()
{
	m_pCollection = NULL;
}
	
// The class destructor

CCollection::~CCollection()
{
	SAFE_RELEASE(m_pCollection);
}

// Enumerate instruments stored in a collection given the index in the collection
// returns a pointer to an INSTRUMENTINFO

HRESULT CCollection::EnumInstrument(DWORD dwIndex,LPINSTRUMENTINFO InstInfo)
{
	HRESULT hr = DM_FAILED;
    DWORD dwPatch;
	TCHAR strMembrFunc[] = _T("CCollection::EnumIntrument()");
	WCHAR wszInstName[MAX_PATH];
	
	if (m_pCollection && InstInfo)
	{	
		hr = m_pCollection->EnumInstrument(dwIndex,&dwPatch,wszInstName,MAX_PATH);
		if ((hr == E_FAIL) || (hr == E_OUTOFMEMORY) || (hr == E_POINTER)) throw CDMusicException(strMembrFunc,hr,__LINE__);
		
		if (hr == S_OK)
		{
			#ifdef _UNICODE
				_tcscpy(InstInfo->szInstName,wszInstName);
			#else
				wcstombs(InstInfo->szInstName,wszInstName,MAX_PATH);
			#endif

			InstInfo->dwPatchInCollection = dwPatch; // Patch in the collection	
		}
	
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	
	return hr;
}


// Get the instrument given a pointer to an INSTRUMENTINFO structure

HRESULT CCollection::GetInstrument(CInstrument &Instrument,LPINSTRUMENTINFO InstInfo)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CCollection::GetIntrument()/structure");

	if (m_pCollection && InstInfo)
	{
		
		SAFE_RELEASE(Instrument.m_pInstrument); // Release the old reference to the interface
		
		// Gets the instrument given the patch in the collection
		
		if (FAILED(hr = m_pCollection->GetInstrument(InstInfo->dwPatchInCollection,&Instrument.m_pInstrument))) 
			 throw CDMusicException(strMembrFunc,hr,__LINE__);
		
		// Fills with information the class public member variables

		Instrument.m_dwPatchInCollection = InstInfo->dwPatchInCollection;
		_tcscpy(Instrument.m_strInstName,InstInfo->szInstName);
	
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}

// Get the instrument given the index in the collection

HRESULT CCollection::GetInstrument(CInstrument &Instrument,INT nIndex)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CCollection::GetIntrument()/index");

	INSTRUMENTINFO InstInfo;

	if (m_pCollection)
	{
		SAFE_RELEASE(Instrument.m_pInstrument); // Release the old reference to the interface
		
		// Gets the instrument associated with the index
		
		if (FAILED(hr = EnumInstrument(nIndex,&InstInfo))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); 
		
		// Gets the instrument given the patch in the collection
		
		if (FAILED(hr = m_pCollection->GetInstrument(InstInfo.dwPatchInCollection,&Instrument.m_pInstrument))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__); 
		
		// Fills with information the class public member variables

		Instrument.m_dwPatchInCollection = InstInfo.dwPatchInCollection;
		
		_tcscpy(Instrument.m_strInstName,InstInfo.szInstName);
	
	} else throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return S_OK;
}


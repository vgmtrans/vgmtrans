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
Module : C3DSegment.cpp
Purpose: Code implementation for C3DSegment class
Created: CJP / 02-08-2002
History: CJP / 03-10-2003 
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
#include "CAudioPart.h"
#include <atlbase.h>

using namespace directmidi;

// Constructor

C3DSegment::C3DSegment()
{
	m_bExternalAPath = FALSE;
}


// Destructor

C3DSegment::~C3DSegment()
{
	ReleaseSegment();
}

// Initializes the 3D segment given a source audiopath performance

HRESULT C3DSegment::Initialize(CAPathPerformance &pPerformance,DWORD dwType,DWORD dwPChannelCount)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("C3DSegment::Initialize(CAPathPerformance)");
 	
	pPerformance.CreateAudioPath(m_AudioPath,dwType,dwPChannelCount,TRUE);
	 
	m_AudioPath.Get3DBuffer(*this);
	m_AudioPath.Get3DListener(*this);

	m_bExternalAPath = FALSE;

	return S_OK;
}

// Initializes the 3D segment given an external audiopath 

HRESULT C3DSegment::Initialize(CAudioPath &Path)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("C3DSegment::Initialize(CAudioPath)");
 	
	m_AudioPath = Path;
	m_bExternalAPath = TRUE;
	
	m_AudioPath.Get3DBuffer(*this);
	
	return S_OK;
}

// Releases the segment and its internal objects

HRESULT C3DSegment::ReleaseSegment()
{
	ReleaseBuffer();
	ReleaseListener();
	CSegment::ReleaseSegment();
	
	if (m_bExternalAPath)
		m_AudioPath.m_pPath = NULL;
	
	m_AudioPath.ReleaseAudioPath();
	
	return S_OK;
}

// Sets the 3D segment volume

HRESULT C3DSegment::SetVolume(long lVolume,DWORD dwDuration)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("C3DSegment::SetVolume()");
 
	if (m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return m_AudioPath.SetVolume(lVolume,dwDuration);
}

// Retrieves an interface for an object in the audiopath

HRESULT C3DSegment::GetObjectInPath(DWORD dwPChannel, DWORD dwStage, DWORD dwBuffer, REFGUID guidObject,
								    DWORD dwIndex,REFGUID iidInterface,void **ppObject)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("C3DSegment::GetObjectInPath()");
 
	if (m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return m_AudioPath.GetObjectInPath(dwPChannel, dwStage, dwBuffer, guidObject,
								       dwIndex, iidInterface,ppObject);
}
	
// Gets the internal audiopath

CAudioPath& C3DSegment::GetAudioPath()
{
	return m_AudioPath;
}

// First = operator. Clones a 3D segment

C3DSegment& C3DSegment::operator = (const C3DSegment &Segment)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("C3DSegment::operator=()");

	if (Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CComPtr<IDirectMusicSegment> pOldSegment;

	if (FAILED(hr = Segment.m_pSegment->Clone(0,0,&pOldSegment)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = pOldSegment->QueryInterface(IID_IDirectMusicSegment8,(void**)&m_pSegment)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return *this;
}

// Second = operator. Clones a segment and returns the corresponding 3D segment

C3DSegment& C3DSegment::operator = (const CSegment &Segment)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("(C3DSegment)CSegment::operator=(CSegment)");

	if (Segment.m_pSegment == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	CComPtr<IDirectMusicSegment> pOldSegment;

	if (FAILED(hr = Segment.m_pSegment->Clone(0,0,&pOldSegment)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = pOldSegment->QueryInterface(IID_IDirectMusicSegment8,(void**)&m_pSegment)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return *this;
}



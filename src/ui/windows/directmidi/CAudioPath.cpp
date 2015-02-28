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
Module : CAudioPath.cpp
Purpose: Code implementation for CAudioPath class
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
#include "CAudioPart.h"

using namespace directmidi;

// Constructor

CAudioPath::CAudioPath()
{
	m_pPath	= NULL;
}

// Destructor

CAudioPath::~CAudioPath()
{
	ReleaseAudioPath();
}


// Activates/deactivates an audiopath

HRESULT CAudioPath::Activate(BOOL bActivate)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAudioPath::Activate()");

	if (m_pPath == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPath->Activate(bActivate)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return hr;
}

// Sets the volume for the audiopath

HRESULT CAudioPath::SetVolume(long lVolume,DWORD dwDuration)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAudioPath::SetVolume()");

	if (m_pPath == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPath->SetVolume(lVolume,dwDuration)))
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Gets a 3D directsound buffer

HRESULT CAudioPath::Get3DBuffer(C3DBuffer &_3DBuffer)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAudioPath::Get3DBuffer()");

	if (m_pPath == NULL || _3DBuffer.m_pBuffer)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPath->GetObjectInPath(DMUS_PCHANNEL_ALL,
		DMUS_PATH_BUFFER,0,GUID_NULL,0,IID_IDirectSound3DBuffer,
		(void**)&_3DBuffer.m_pBuffer))) throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}

// Gets a 3D directsound listener

HRESULT CAudioPath::Get3DListener(C3DListener &_3DListener)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAudioPath::Get3DListener()");

	if (m_pPath == NULL || _3DListener.m_pListener)
		throw CDMusicException(strMembrFunc,hr,__LINE__);

	if (FAILED(hr = m_pPath->GetObjectInPath(0,DMUS_PATH_PRIMARY_BUFFER,0,
	   GUID_NULL,0,IID_IDirectSound3DListener,(void**)&_3DListener.m_pListener))) 
			throw CDMusicException(strMembrFunc,hr,__LINE__);

	return hr;
}


// Retrieves an interface for an object in the audiopath

HRESULT CAudioPath::GetObjectInPath(DWORD dwPChannel, DWORD dwStage, DWORD dwBuffer, REFGUID guidObject,
									DWORD dwIndex,REFGUID iidInterface,void **ppObject)
{
	HRESULT hr = DM_FAILED;
	TCHAR strMembrFunc[] = _T("CAudioPath::GetObjectInPath()");

	if (m_pPath == NULL)
		throw CDMusicException(strMembrFunc,hr,__LINE__);
 
	if (FAILED(hr = m_pPath->GetObjectInPath(dwPChannel,dwStage,dwBuffer,guidObject,
					dwIndex,iidInterface,ppObject)))
			   throw CDMusicException(strMembrFunc,hr,__LINE__);
	
	return hr;
}

// Releases the audiopath

HRESULT CAudioPath::ReleaseAudioPath()
{
	if (m_pPath)
	{	
		SAFE_RELEASE(m_pPath);
		return S_OK;
	} else return S_FALSE;
}
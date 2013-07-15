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
Module : C3DListener.cpp
Purpose: Code implementation for C3DListener class
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
#include "stdafx.h"
#include "CAudioPart.h"

using namespace directmidi;

// Constructor

C3DListener::C3DListener()
{
	m_pListener	= NULL;
}

// Destructor

C3DListener::~C3DListener()
{
	ReleaseListener();
}

// Commits any deferred settings made since the last call to this method

HRESULT C3DListener::CommitDeferredSettings()
{
	TCHAR strMembrFunc[] = _T("C3DListener::CommitDeferredSettings()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->CommitDeferredSettings());
	return S_OK;
}

// Retrieves all 3-D parameters of the sound environment and the listener

HRESULT C3DListener::GetAllListenerParameters(LPDS3DLISTENER pListener)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetAllListenerParameters()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetAllParameters(pListener));
	return S_OK;
}

// Sets all 3-D parameters of the sound environment and the listener

HRESULT C3DListener::SetAllListenerParameters(LPCDS3DLISTENER pcListener,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener::SetAllListenerParameters()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pcListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetAllParameters(pcListener,dwApply));
	return S_OK;
}

// Retrieves the distance factor, which is the number of meters in a vector unit

HRESULT C3DListener::GetDistanceFactor(D3DVALUE *pflDistanceFactor)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetDistanceFactor()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pflDistanceFactor==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetDistanceFactor(pflDistanceFactor));
	return S_OK;
}

// Retrieves the multiplier for the Doppler effect

HRESULT C3DListener::GetDopplerFactor(D3DVALUE *pflDopplerFactor)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetDopplerFactor()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pflDopplerFactor==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetDopplerFactor(pflDopplerFactor));
	return S_OK;
}

// Retrieves the rolloff factor, which determines the rate of attenuation over distance

HRESULT C3DListener::GetRolloffFactor(D3DVALUE *pflRolloffFactor)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetRolloffFactor()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pflRolloffFactor==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetRolloffFactor(pflRolloffFactor));
	return S_OK;
}

// Sets the distance factor

HRESULT C3DListener::SetDistanceFactor(D3DVALUE flDistanceFactor,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener::SetDistanceFactor()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetDistanceFactor(flDistanceFactor,dwApply));
	return S_OK;
}

// Sets the multiplier for the Doppler effect

HRESULT C3DListener::SetDopplerFactor(D3DVALUE flDopplerFactor,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener:SetDopplerFactor()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetDopplerFactor(flDopplerFactor,dwApply));
	return S_OK;
}

// Sets the rolloff factor

HRESULT C3DListener::SetRolloffFactor(D3DVALUE flRolloffFactor,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener::SetRolloffFactor()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetRolloffFactor(flRolloffFactor,dwApply));
	return S_OK;
}

// Retrieves the orientation of the listener's head

HRESULT C3DListener::GetOrientation(D3DVECTOR *pvOrientFront,D3DVECTOR *pvOrientTop)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetOrientation()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pvOrientFront==NULL,pvOrientTop==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetOrientation(pvOrientFront,pvOrientTop));
	return S_OK;
}

// Retrieves the listener's position

HRESULT C3DListener::GetListenerPosition(D3DVECTOR *pvPosition)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetListenerPosition()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pvPosition==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetPosition(pvPosition));
	return S_OK;
}

// Retrieves the listener's velocity

HRESULT C3DListener::GetVelocity(D3DVECTOR *pvVelocity)
{
	TCHAR strMembrFunc[] = _T("C3DListener::GetVelocity()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL,pvVelocity==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->GetVelocity(pvVelocity));
	return S_OK;
}

// Sets the orientation of the listener's head

HRESULT C3DListener::SetOrientation(D3DVALUE xFront,D3DVALUE yFront,D3DVALUE zFront,D3DVALUE xTop,
D3DVALUE yTop,D3DVALUE zTop,DWORD  dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener::SetOrientation()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetOrientation(xFront,yFront,zFront,xTop,yTop,zTop,dwApply));
	return S_OK;
}

// Sets the listener's position

HRESULT C3DListener::SetListenerPosition(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener::SetListenerPosition()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetPosition(x,y,z,dwApply));
	return S_OK;
}

// Sets the listener's velocity

HRESULT C3DListener::SetVelocity(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DListener::SetVelocity()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pListener==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pListener->SetVelocity(x,y,z,dwApply));
	return S_OK;
}

// Releases the listener object

HRESULT C3DListener::ReleaseListener()
{
	if (m_pListener)
	{
		SAFE_RELEASE(m_pListener);
		return S_OK;
	} else return S_FALSE;
}



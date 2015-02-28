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
Module : C3DBuffer.cpp
Purpose: Code implementation for C3DBuffer class
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

C3DBuffer::C3DBuffer()
{
	m_pBuffer = NULL;
}

// Destructor

C3DBuffer::~C3DBuffer()
{
	ReleaseBuffer();
}

// Retrieves the operation mode for 3-D sound processing

HRESULT C3DBuffer::GetMode(LPDWORD pdwMode)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetMode()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pdwMode==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetMode(pdwMode));
	return S_OK;
}

// Sets the operation mode for 3-D sound processing

HRESULT C3DBuffer::SetMode(DWORD dwMode,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetMode()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetMode(dwMode,dwApply));
	return S_OK;
}

// Retrieves all 3-D properties of the sound buffer

HRESULT C3DBuffer::GetAllBufferParameters(LPDS3DBUFFER pDs3dBuffer)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetAllParameters()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pDs3dBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetAllParameters(pDs3dBuffer));
	return S_OK;
}

// Sets all 3-D properties of the sound buffer

HRESULT C3DBuffer::SetAllBufferParameters(LPCDS3DBUFFER pcDs3dBuffer,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetAllParameters()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pcDs3dBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetAllParameters(pcDs3dBuffer,dwApply));
	return S_OK;
}

// Retrieves the maximum distance

HRESULT C3DBuffer::GetMaxDistance(D3DVALUE *pflMaxDistance)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetMaxDistance()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pflMaxDistance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetMaxDistance(pflMaxDistance));
	return S_OK;
}

// Retrieves retrieves the minimum distance

HRESULT C3DBuffer::GetMinDistance(D3DVALUE *pflMinDistance)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetMinDistance()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pflMinDistance==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetMinDistance(pflMinDistance));
	return S_OK;
}

// Sets the maximum distance

HRESULT C3DBuffer::SetMaxDistance(D3DVALUE flMaxDistance,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetMaxDistance()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetMaxDistance(flMaxDistance,dwApply));
	return S_OK;
}

// Sets the minimum distance

HRESULT C3DBuffer::SetMinDistance(D3DVALUE flMinDistance,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetMinDistance()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetMinDistance(flMinDistance,dwApply));
	return S_OK;
}

// Retrieves the position of the sound source

HRESULT C3DBuffer::GetBufferPosition(D3DVECTOR *pvPosition)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetPosition()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pvPosition==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetPosition(pvPosition));
	return S_OK;
}

// Sets the position of the sound source

HRESULT C3DBuffer::SetBufferPosition(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetPosition()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetPosition(x,y,z,dwApply));
	return S_OK;
}

// Retrieves the inside and outside angles of the sound projection cone

HRESULT C3DBuffer::GetConeAngles(LPDWORD pdwInsideConeAngle,LPDWORD pdwOutsideConeAngle)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetConeAngles()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pdwInsideConeAngle==NULL,pdwOutsideConeAngle==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetConeAngles(pdwInsideConeAngle,pdwOutsideConeAngle));
	return S_OK;
}

// Retrieves the orientation of the sound projection cone

HRESULT C3DBuffer::GetConeOrientation(D3DVECTOR *pvOrientation)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetConeOrientation()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pvOrientation==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetConeOrientation(pvOrientation));
	return S_OK;
}

// Retrieves the volume of the sound outside the outside angle of the sound projection cone

HRESULT C3DBuffer::GetConeOutsideVolume(LPLONG plConeOutsideVolume)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetConeOutsideVolume()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,plConeOutsideVolume==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetConeOutsideVolume(plConeOutsideVolume));
	return S_OK;
}

// Sets the inside and outside angles of the sound projection cone

HRESULT C3DBuffer::SetConeAngles(DWORD dwInsideConeAngle,DWORD dwOutsideConeAngle,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetConeAngles()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetConeAngles(dwInsideConeAngle,dwOutsideConeAngle,dwApply));
	return S_OK;
}

// Sets the orientation of the sound projection cone

HRESULT C3DBuffer::SetConeOrientation(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
{	
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetConeOrientation()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetConeOrientation(x,y,z,dwApply));
	return S_OK;
}

// Sets the volume of the sound outside the outside angle of the sound projection cone

HRESULT C3DBuffer::SetConeOutsideVolume(LONG lConeOutsideVolume,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetConeOutsideVolume()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetConeOutsideVolume(lConeOutsideVolume,dwApply));
	return S_OK;
}

// Retrieves the velocity of the sound source

HRESULT C3DBuffer::GetVelocity(D3DVECTOR *pvVelocity)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::GetVelocity()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL,pvVelocity==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->GetVelocity(pvVelocity));
	return S_OK;
}

// Sets the velocity of the sound source

HRESULT C3DBuffer::SetVelocity(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
{
	TCHAR strMembrFunc[] = _T("C3DBuffer::SetVelocity()");
	DM_ASSERT(strMembrFunc,__LINE__,m_pBuffer==NULL);
	DM_ASSERT_HR(strMembrFunc,__LINE__,m_pBuffer->SetVelocity(x,y,z,dwApply));
	return S_OK;
}

// Releases the buffer

HRESULT C3DBuffer::ReleaseBuffer()
{
	if (m_pBuffer)
	{	
		SAFE_RELEASE(m_pBuffer);
		return S_OK;
	} else return S_FALSE;
}



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
Module : CDMusicException.cpp
Purpose: Code implementation for the CDMusicException class
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
#include "CDirectMidi.h"
#include <DxErr.h>

using namespace directmidi;

// Constructor, passes the error parameters

CDMusicException::CDMusicException(LPCTSTR lpMemberFunc,HRESULT hr,int nLine) throw()
{
	m_hrCode = hr;
	m_nLine	 = nLine;
	_tcscpy(m_strMethod,lpMemberFunc);
}
		

// Copies a defined message in the suitable member variable

void CDMusicException::CopyMsg(LPCTSTR strDefMsg,LPCTSTR strDescMsg)
{
	_tcscpy(m_strMessage,strDefMsg);
	_tcscat(m_strMessage,_T(": "));
	_tcscat(m_strMessage,strDescMsg);
}


// Obtains the error definition string in the m_strMessage member

void CDMusicException::ObtainErrorString()
{

	switch(m_hrCode)
	{
		case DM_FAILED:
			CopyMsg(_T("DM_FAILED:"),_T("There has been an error during the ") \
			_T("\nobject initialization sequence.\nThere is an internal member variable,") \
			_T("\nsuch as an interface pointer, that it is not assigned"));
			break;
		default:
			CopyMsg(DXGetErrorString(m_hrCode),DXGetErrorDescription(m_hrCode));
			break;
	}
}

// Obtains only the error definition

LPCTSTR CDMusicException::ErrorToString() throw()
{
	ObtainErrorString();
	return m_strMessage;
}

// Returns the error description

LPCTSTR CDMusicException::GetErrorDescription() throw()
{
	ObtainErrorString();
	// Get an error description using FormatMessage
	wsprintf(m_strErrorBuf, _T("Error code: 0x%.8x \n%s\nMember function: %s \nLine: %d"),
	m_hrCode, m_strMessage, m_strMethod,m_nLine); 
	return m_strErrorBuf; // Returns the string description
}	
		
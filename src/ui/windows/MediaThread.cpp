#include "stdafx.h"
#include "MediaThread.h"

typedef DECLSPEC_IMPORT UINT (WINAPI* LPTIMEBEGINPERIOD)( UINT uPeriod );

MediaThread mediaThread;

MediaThread::MediaThread()
{
}

MediaThread::~MediaThread()
{
}

bool MediaThread::Init()
{
	// Increase the accuracy of Sleep() without needing to link to winmm.lib
    HINSTANCE hInstWinMM = LoadLibrary( L"winmm.dll" );
    if( hInstWinMM ) 
    {
        LPTIMEBEGINPERIOD pTimeBeginPeriod = (LPTIMEBEGINPERIOD)GetProcAddress( hInstWinMM, "timeBeginPeriod" );
        if( NULL != pTimeBeginPeriod )
            pTimeBeginPeriod(1);

        FreeLibrary(hInstWinMM);
    }
	return true;
}

void MediaThread::Run()
{
}

void MediaThread::Terminate()
{
}

DWORD MediaThread::MainMediaThreadLoop(VOID* lpParam)
{
	return 0;
}

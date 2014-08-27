// stdafx.cpp : source file that includes just the standard includes
//	VGMTrans.pch will be the pre-compiled header
//	stdafx.obj will contain the pre-compiled type information

#ifdef _WIN32
	#include "stdafx.h"
#endif

#if (_ATL_VER < 0x0700)
#include <atlimpl.cpp>
#endif //(_ATL_VER < 0x0700)

#include <dockimpl.cpp>
//#include <TabbedMDISave.cpp>

#if (_MSC_VER < 1300)
	// We've specified _TABBEDMDI_MESSAGES_EXTERN_REGISTER
	// (see TabbedMDI.h)
	RegisterTabbedMDIMessages g_RegisterTabbedMDIMessages;
#endif
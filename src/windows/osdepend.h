#if !defined(OSDEPEND_H)
#define OSDEPEND_H

#include <stdarg.h>		//support for variadic functions

#define Alert(...)						\
	{									\
		wchar_t buffer[1024];			\
		wsprintf(buffer, __VA_ARGS__);	\
		ShowAlertMessage(buffer);		\
	}						

void ShowAlertMessage(wchar_t* msg);

#endif // !defined(COMMON_H)

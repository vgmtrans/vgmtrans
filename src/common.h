#if !defined(COMMON_H)
#define COMMON_H

using namespace std;

#include "osdepend.h"
#include "helper.h"
#include "types.h"

#define VERSION "1.0.3"

#define KILOBYTE 1024
#define MEGABYTE (KILOBYTE*1024)
#define GIGABYTE (MEGABYTE*1024)

#define F_EPSILON 0.00001

#define FORWARD_DECLARE_TYPEDEF_STRUCT(type) \
	struct _##type;	\
	typedef _##type type

std::wstring StringToUpper(std::wstring myString);
std::wstring StringToLower(std::wstring myString);

//#define for_each(_ITER_, _COLL_) for (auto _ITER_ = _COLL_.begin(); \
//    _ITER_ != _COLL_.end(); _ITER_++)

/**
Converts a std::string to any class with a proper overload of the >> opertor
@param temp			The string to be converted
@param out	[OUT]	The container for the returned value
*/
template < class T >
	void FromString( const std::string& temp, T* out )
{
	std::istringstream val( temp );
	val >> *out;

	assert(! val.fail() );
}

U32 StringToHex( const std::string& str );

inline string wstring2string(wstring& wstr)
{
	char *mbs = new char[wstr.length() * MB_CUR_MAX + 1];
	wcstombs(mbs, wstr.c_str(), wstr.length() * MB_CUR_MAX + 1);
	string str(mbs);
	delete[] mbs;
	return str;
}

inline wstring string2wstring(string& str)
{
	wchar_t *wcs = new wchar_t[str.length() + 1];
	mbstowcs(wcs, str.c_str(), str.length() + 1);
	wstring wstr(wcs);
	delete[] wcs;
	return wstr;
}

//string WstringToString(wstring& wstr)
//{
//	stringstream stream;
//	stream << wstr;
//	return stream.str();
//}
//
//wstring StringToWstring(string& str)
//{
//	wostringstream stream;
//	stream << str;
//	return stream.str();
//}

inline int CountBytesOfVal(BYTE* buf, UINT numBytes, BYTE val)
{
	int count = 0;
	for (UINT i=0; i<numBytes; i++)
		if (buf[i] == val)
			count++;
	return count;
}

 inline bool isEqual(float x, float y)
 {
   //const double epsilon = 0.00001/* some small number such as 1e-5 */;
   return std::abs(x - y) <= F_EPSILON * std::abs(x);
   // see Knuth section 4.2.2 pages 217-218
 } 

 inline int round(double x)
 {
	 return (x > 0) ? (int)(x + 0.5) : (int)(x - 0.5);
 }

 //inline BYTE round(double x)
 //{
	// return (BYTE)(x + 0.5);
 //}

#endif // !defined(COMMON_H)

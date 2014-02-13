#pragma once

#include <vector>
#include "types.h"

template <class T> void DeleteVect(std::vector<T*> &theArray)
{
	int nArraySize = (int)theArray.size();
	for (int i=0; i<nArraySize; i++)
		delete theArray[i];
	theArray.clear();
}

template <class T> void DeleteList(std::list<T*> &theList)
{
	//int nArraySize = (int)theArray.size();
	for (std::list<T*>::iterator iter = theList.begin(); iter != theList.end(); ++iter)
		delete (*iter);
	theList.clear();
}

template <class T1, class T2> void DeleteHashMap(std::hash_map<T1,T2*> &container)
{
	//int nArraySize = (int)theArray.size();
	for (std::hash_map<T1,T2*>::iterator iter = container.begin(); iter != container.end(); ++iter)
		delete (*iter).second;
	container.clear();
}

template <class T1, class T2> void DeleteMap(std::map<T1,T2*> &container)
{
	//int nArraySize = (int)theArray.size();
	for (std::map<T1,T2*>::iterator iter = container.begin(); iter != container.end(); ++iter)
		delete (*iter).second;
	container.clear();
}




template <class T> inline void PushTypeOnVect(std::vector<BYTE> &theVector, T unit)
{
	UINT i = sizeof(T);
	theVector.insert(theVector.end(), ((BYTE*)&unit), ((BYTE*)(&unit))+sizeof(T) );
}

template <class T> inline void PushTypeOnVectBE(std::vector<BYTE> &theVector, T unit)
{
	//::reverse_iterator<BYTE*> begin = ::reverse_iterator<BYTE*>((BYTE*)&unit);
	//::reverse_iterator<BYTE*> end = ::reverse_iterator<BYTE*>(((BYTE*)(&unit))+sizeof(T));
	//theVector.insert(theVector.end(), begin, end);
	for (UINT i=0; i<sizeof(T); i++)
		theVector.push_back(*(((BYTE*)(&unit))-i+sizeof(T)-1));
}


//inline void PushBackWordOnVectorBE(vector<BYTE> &theVector, UINT theWord)
//{
//	theVector.insert(theVector.end(), ((BYTE*)&theWord), ((BYTE*)((&theWord)+sizeof(theWord))) );
//}

//inline void PushBackShortOnVectorBE(vector<BYTE> &theVector, USHORT theShort)
//{
//	theVector.insert(theVector.end(), ((BYTE*)&theShort), ((BYTE*)((&theShort)+sizeof(theShort))) );
//}

inline void PushBackStringOnVector(std::vector<BYTE> &theVector, std::string &str)
{
	theVector.insert(theVector.end(), str.data(), str.data()+str.length());
}

template <class Tstring> inline Tstring FormatString(const Tstring fmt, ...)
{
	va_list va;
	std::vector<Tstring::value_type> output(128);
	while (true)
	{
		va_start(va, fmt);
		int n = _vsnwprintf(&output[0], output.size(), fmt.c_str(), va);
		va_end(va);

		if (n != -1 && (size_t)n < output.size())
			break;

		output.resize(output.size() * 2);
	}
	return Tstring(output.begin(), output.end());
}

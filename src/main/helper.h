#pragma once

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
	for (typename std::list<T*>::iterator iter = theList.begin(); iter != theList.end(); ++iter)
		delete (*iter);
	theList.clear();
}

template <class T1, class T2> void DeleteMap(std::map<T1,T2*> &container)
{
	//int nArraySize = (int)theArray.size();
	for (typename std::map<T1,T2*>::iterator iter = container.begin(); iter != container.end(); ++iter)
		delete (*iter).second;
	container.clear();
}




template <class T> inline void PushTypeOnVect(std::vector<uint8_t> &theVector, T unit)
{
	theVector.insert(theVector.end(), ((uint8_t*)&unit), ((uint8_t*)(&unit))+sizeof(T) );
}

template <class T> inline void PushTypeOnVectBE(std::vector<uint8_t> &theVector, T unit)
{
	//::reverse_iterator<uint8_t*> begin = ::reverse_iterator<uint8_t*>((uint8_t*)&unit);
	//::reverse_iterator<uint8_t*> end = ::reverse_iterator<uint8_t*>(((uint8_t*)(&unit))+sizeof(T));
	//theVector.insert(theVector.end(), begin, end);
	for (uint32_t i=0; i<sizeof(T); i++)
		theVector.push_back(*(((uint8_t*)(&unit))-i+sizeof(T)-1));
}


//inline void PushBackWordOnVectorBE(vector<uint8_t> &theVector, uint32_t theWord)
//{
//	theVector.insert(theVector.end(), ((uint8_t*)&theWord), ((uint8_t*)((&theWord)+sizeof(theWord))) );
//}

//inline void PushBackShortOnVectorBE(vector<uint8_t> &theVector, uint16_t theShort)
//{
//	theVector.insert(theVector.end(), ((uint8_t*)&theShort), ((uint8_t*)((&theShort)+sizeof(theShort))) );
//}

inline void PushBackStringOnVector(std::vector<uint8_t> &theVector, std::string &str)
{
	theVector.insert(theVector.end(), str.data(), str.data()+str.length());
}

template <class Tstring> inline Tstring FormatString(const Tstring fmt, ...)
{
	va_list va;
	std::vector<typename Tstring::value_type> output(128);
	while (true)
	{
		va_start(va, fmt);
		int n = swprintf(&output[0], output.size(), fmt.c_str(), va);
		va_end(va);

		if (n != -1 && (size_t)n < output.size())
			break;

		output.resize(output.size() * 2);
	}
	return Tstring(output.begin(), output.end());
}

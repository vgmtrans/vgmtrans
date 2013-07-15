#include "stdafx.h"
#include "common.h"

std::wstring StringToUpper(std::wstring myString)
{
  const int length = myString.length();
  for(int i=0; i!=length; ++i)
  {
    myString[i] = toupper(myString[i]);
  }
  return myString;
}

std::wstring StringToLower(std::wstring myString)
{
  const int length = myString.length();
  for(int i=0; i!=length; ++i)
  {
    myString[i] = tolower(myString[i]);
  }
  return myString;
}

U32 StringToHex( const std::string& str )
{
	U32 value;
	stringstream convert ( str );			
	convert >> std::hex >> value;		//read seq_table as hexadecimal value
	return value;
}

#pragma once

#include <unordered_map>

// For usage in std::unordered_map, hash of type LPCTSTR
class CHashCompareLPCTSTR
{
public:
	CHashCompareLPCTSTR() {}
	size_t operator( )(const LPCTSTR& Key) const;
};

// For usage in std::unordered_map, equality of type LPCTSTR
class CEqualLPCTSTR
{
public:
	bool operator( )(const LPCTSTR& _Key1, const LPCTSTR& _Key2) const { return _tcscmp(_Key1, _Key2) == 0; }
};

// Hash map with LPCTSTR as key (compares string content, not pointers)
template<class TValue>
using LPCTSTRHashMap = std::unordered_map<LPCTSTR, TValue, CHashCompareLPCTSTR, CEqualLPCTSTR>;

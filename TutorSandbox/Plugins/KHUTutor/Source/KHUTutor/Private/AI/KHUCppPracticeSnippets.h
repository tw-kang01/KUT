#pragma once
// These two problem statements are also compiled by developer tests. Neither is a solution hint.
#define KHU_CPP_FLOW bool CanOpen(bool bHasKey, int32 Energy) { return bHasKey && Energy >= 3; }
#define KHU_CPP_REVIEW int32 First(const TArray<int32>& Values) { return Values[0]; }
#define KHU_CPP_STRING_IMPL(Code) #Code
#define KHU_CPP_STRING(Code) KHU_CPP_STRING_IMPL(Code)

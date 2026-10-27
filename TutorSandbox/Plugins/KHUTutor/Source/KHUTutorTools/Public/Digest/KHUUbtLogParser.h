#pragma once
#include "CoreMinimal.h"

/** A compiler/linker error with a project-relative or explicitly external filename. */
struct KHUTUTORTOOLS_API FKHUCppDiagnostic
{
    FString File;
    int32 Line = 0;
    FString Code;
    FString Message;
};
/** Latest observed build, including incomplete/unknown states rather than inferred success. */
struct KHUTUTORTOOLS_API FKHUCppBuildSnapshot
{
    FString Status = TEXT("unknown");
    FString Kind = TEXT("UBT");
    FString Log;
    FDateTime Time;
    TArray<FKHUCppDiagnostic> Errors;
    bool bDiagnosticsTruncated = false;
};
/** Pure UBT/MSVC/UHT parser; never starts a build or reads files. */
class KHUTUTORTOOLS_API FKHUUbtLogParser
{
public:
    /** Verify the final build command identifies exactly this absolute .uproject. */
    static bool IsProjectBuild(const FString& Text, const FString& ProjectFile);
    /** Parse the last command segment, deduplicate errors and recognize explicit outcomes. */
    static FKHUCppBuildSnapshot Parse(const FString& Text, const FString& ProjectDir);
    /** Compact diagnostics, masks sensitive text, and limits output to 6000 characters. */
    static FString Describe(const FKHUCppBuildSnapshot& Snapshot, int32 MaxErrors);
};

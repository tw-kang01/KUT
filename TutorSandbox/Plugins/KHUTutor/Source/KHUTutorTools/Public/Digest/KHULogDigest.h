#pragma once
#include "Sensors/KHULogCapture.h"

/** Pure digest options; MaxLines limits message groups, excluding the summary header. */
struct KHUTUTORTOOLS_API FKHULogDigestOptions
{
    FString MinSeverity = TEXT("Warning");
    double SinceSeconds = 600;
    int32 MaxLines = 40;
    TSet<FName> NoiseCategories;
};

/** Pure bounded formatting and privacy helpers shared by read-only tools. */
class KHUTUTORTOOLS_API FKHULogDigest
{
public:
    /** Filter, group normalized duplicates, and preserve the first error's nearby context. */
    static FString Build(const TArray<FKHULogEntry>& Entries, double Now, const FKHULogDigestOptions& Options);
    /** Mask user profile paths and common API-key/header forms before returning text. */
    static FString Sanitize(const FString& Text);
    /** Preserve a visible truncation marker within the character limit. */
    static FString Bound(const FString& Text, int32 MaxChars = 6000);
};

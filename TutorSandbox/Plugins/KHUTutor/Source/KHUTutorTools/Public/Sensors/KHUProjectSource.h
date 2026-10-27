#pragma once
#include "CoreMinimal.h"

/** Bounded local-file reading; Source reads verify final Windows handle paths. */
class KHUTUTORTOOLS_API FKHUProjectSource
{
public:
    /** Resolve a project-relative Source path; reject absolute paths, ADS and traversal. */
    static bool Resolve(const FString& ProjectDir, const FString& RelativePath, FString& Absolute);
    /** Check canonical directory boundaries, including the separator after the root. */
    static bool IsUnderRoot(const FString& Root, const FString& File);
    /** Read <=1 MiB from Source, preserving blank lines, masking sensitive text and numbering. */
    static FString Read(const FString& ProjectDir, const FString& RelativePath, int32 Line, int32 Radius);
    /** Read a fixed sensor log up to MaxBytes, never an arbitrary model-supplied path. */
    static bool ReadBoundedFile(const FString& Absolute, int32 MaxBytes, FString& Text, FString& Issue);
};

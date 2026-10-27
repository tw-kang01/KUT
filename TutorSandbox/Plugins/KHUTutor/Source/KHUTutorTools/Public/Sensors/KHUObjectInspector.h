#pragma once
#include "CoreMinimal.h"
class UWorld;

/** Bounded reflection snapshots. Never load assets, invoke getters or change editor state. */
class KHUTUTORTOOLS_API FKHUObjectInspector
{
public:
    /** Resolve only the requested Editor/PIE world; no fallback between worlds. */
    static UWorld* World(const FString& Scope, FString& Issue);
    /** Resolve an exact loaded actor/component path belonging to World. */
    static UObject* Find(UWorld* World, const FString& Path, FString& Issue);
    /** Read actor/component reference edges with cycle, depth, count and output limits. */
    static FString Relations(UWorld* World, const TArray<UObject*>& Roots, int32 Depth, int32 MaxObjects);
    /** Read visible/editable properties and compare to class/archetype defaults. Null World requires a CDO. */
    static FString Details(UObject* Object, UWorld* World, const FString& PropertyNames, int32 MaxProperties);
};

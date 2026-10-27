#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Digest/KHUUbtLogParser.h"

class UBlueprint;
/** Last observed Blueprint compile; status and diagnostic counts remain distinct. */
struct KHUTUTORTOOLS_API FKHUBlueprintCompileState
{
    FString AssetPath;
    FString Name;
    FString Status;
    int32 Errors = 0;
    int32 Warnings = 0;
    double Time = 0;
};
/** Game-thread state. A patch notification does not prove a full C++ build succeeded. */
struct KHUTUTORTOOLS_API FKHUBuildState
{
    TOptional<FKHUBlueprintCompileState> LastBpCompile;
    TOptional<double> LastCppPatchTime;
    TOptional<FKHUCppBuildSnapshot> LastCpp;
    /** Access sensor state on the game thread. */
    static FKHUBuildState& Get();
};
/** Observes compile events only; never loads assets or starts compilation. */
class KHUTUTORTOOLS_API FKHUCompileWatcher
{
public:
    /** Shared watcher owned by the tools module. */
    static FKHUCompileWatcher& Get();
    /** Subscribe to Blueprint pre-compile and already-loaded Live Coding. */
    void Start();
    /** Remove all global and surviving per-Blueprint delegate registrations. */
    void Stop();
    /** Read current node messages without compiling. */
    static FString DescribeBlueprint(UBlueprint* Blueprint);
    /** Report an explicit loaded asset, or latest compile and currently edited Blueprints. */
    FString GetErrors(const FString& AssetPath);
    /** Return current Live Coding activity without starting it. */
    FString LiveCodingStatus() const;
private:
    void BeforeCompile(UBlueprint* Blueprint);
    void AfterCompile(UBlueprint* Blueprint);
    void PatchComplete();
    void ModulesChanged(FName Name, EModuleChangeReason Reason);
    void AttachLiveCoding();
    FDelegateHandle PreCompileHandle;
    FDelegateHandle ModuleHandle;
    FDelegateHandle PatchHandle;
    TMap<TWeakObjectPtr<UBlueprint>, FDelegateHandle> BlueprintHandles;
    TWeakObjectPtr<UBlueprint> Latest;
    bool bStarted = false;
};

#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Digest/KHUUbtLogParser.h"
#include "Sensors/KHULogCapture.h"

/** Watches fixed UBT log locations and this editor's Live Coding messages. */
class KHUTUTORTOOLS_API FKHUCppBuildWatcher
{
public:
    /** Shared watcher owned by the tools module. */
    static FKHUCppBuildWatcher& Get();
    /** Read existing logs and register a five-second game-thread sensor tick. */
    void Start();
    /** Remove the ticker before module unload; no background job can outlive this module. */
    void Stop();
    /** Compact the last observed C++ status and compiler diagnostics. */
    FString GetErrors(int32 MaxErrors) const;
    /** Observe a successful patch without claiming a full external build succeeded. */
    void PatchComplete();
    /** Apply only a build log for the expected project, and never replace a newer result. */
    static bool ApplyLog(TOptional<FKHUCppBuildSnapshot>& State, const FString& Text,
        const FString& ProjectFile, const FString& Log, FDateTime Time);
    /** Apply current-editor Live Coding completion messages, preserving only recent diagnostics. */
    static bool ApplyLiveMessage(TOptional<FKHUCppBuildSnapshot>& State, const FString& Message, FDateTime Time);
private:
    bool Tick(float DeltaTime);
    FTSTicker::FDelegateHandle TickHandle;
    TMap<FString, FString> Seen;
    double LastLiveTime = 0;
    FString ReadIssue;
    bool bStarted = false;
};

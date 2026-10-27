#include "Sensors/KHUCppBuildWatcher.h"
#include "Sensors/KHUCompileWatcher.h"
#include "Sensors/KHUProjectSource.h"
#include "Digest/KHULogDigest.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

FKHUCppBuildWatcher& FKHUCppBuildWatcher::Get() { static FKHUCppBuildWatcher Watcher; return Watcher; }
void FKHUCppBuildWatcher::Start()
{
    check(IsInGameThread()); if (bStarted) { return; } bStarted = true;
    Tick(0);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FKHUCppBuildWatcher::Tick), 5.0f);
}
void FKHUCppBuildWatcher::Stop()
{
    check(IsInGameThread()); if (!bStarted) { return; } bStarted = false;
    FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset(); Seen.Reset(); ReadIssue.Reset(); LastLiveTime = 0;
}
bool FKHUCppBuildWatcher::ApplyLog(TOptional<FKHUCppBuildSnapshot>& State, const FString& Text,
    const FString& ProjectFile, const FString& Log, FDateTime Time)
{
    if (!FKHUUbtLogParser::IsProjectBuild(Text, ProjectFile) || (State.IsSet() && State.GetValue().Time > Time)) { return false; }
    auto Parsed = FKHUUbtLogParser::Parse(Text, FPaths::GetPath(ProjectFile)); Parsed.Log = Log; Parsed.Time = Time;
    State = MoveTemp(Parsed); return true;
}
bool FKHUCppBuildWatcher::ApplyLiveMessage(TOptional<FKHUCppBuildSnapshot>& State, const FString& Message, FDateTime Time)
{
    FString Status;
    if (Message.StartsWith(TEXT("Live coding succeeded"), ESearchCase::IgnoreCase)) { Status = TEXT("ok"); }
    else if (Message.StartsWith(TEXT("Live coding failed"), ESearchCase::IgnoreCase)) { Status = TEXT("failed"); }
    else if (Message.StartsWith(TEXT("Live coding canceled"), ESearchCase::IgnoreCase)) { Status = TEXT("cancelled"); }
    else if (Message.StartsWith(TEXT("Starting Live Coding compile."), ESearchCase::IgnoreCase)) { Status = TEXT("running"); }
    else { return false; }
    if (State.IsSet() && State.GetValue().Time > Time) { return false; }
    FKHUCppBuildSnapshot Next;
    if (Status == TEXT("failed") && State.IsSet() && (Time - State.GetValue().Time).GetTotalSeconds() <= 60)
    { Next.Errors = State.GetValue().Errors; Next.bDiagnosticsTruncated = State.GetValue().bDiagnosticsTruncated; }
    Next.Status = Status; Next.Kind = TEXT("LiveCoding"); Next.Log = TEXT("current editor log"); Next.Time = Time;
    State = MoveTemp(Next); return true;
}
void FKHUCppBuildWatcher::PatchComplete()
{
    check(IsInGameThread());
    ApplyLiveMessage(FKHUBuildState::Get().LastCpp, TEXT("Live coding succeeded"), FDateTime::UtcNow());
}
bool FKHUCppBuildWatcher::Tick(float DeltaTime)
{
    check(IsInGameThread()); if (!bStarted) { return false; }
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    const FString Own = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Logs/KHU_Build.txt"));
    TArray<FString> Logs = {Own};
    const FString Local = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
    if (!Local.IsEmpty()) { Logs.Add(Local / TEXT("UnrealBuildTool/Log.txt")); }
    for (const FString& Log : Logs)
    {
        const auto Stat = IFileManager::Get().GetStatData(*Log);
        if (!Stat.bIsValid || Stat.bIsDirectory) { continue; }
        const FString Version = FString::Printf(TEXT("%lld|%lld"), Stat.ModificationTime.GetTicks(), Stat.FileSize);
        if (const FString* Previous = Seen.Find(Log)) { if (*Previous == Version) { continue; } }
        FString Text, Issue;
        // Fixed local logs only, at most 2 MiB, only when the metadata changes. No HTTP/future waits.
        if (!FKHUProjectSource::ReadBoundedFile(Log, 2 * 1024 * 1024, Text, Issue))
        { if (Log == Own) { ReadIssue = Issue + TEXT("; showing last observed build"); } continue; }
        const auto After = IFileManager::Get().GetStatData(*Log);
        if (!After.bIsValid || After.FileSize != Stat.FileSize || After.ModificationTime != Stat.ModificationTime) { continue; }
        Seen.Add(Log, Version);
        if (Log == Own) { ReadIssue.Reset(); }
        ApplyLog(FKHUBuildState::Get().LastCpp, Text, Project,
            Log == Own ? TEXT("Saved/Logs/KHU_Build.txt") : TEXT("UnrealBuildTool/Log.txt"), Stat.ModificationTime);
    }
    const double Now = FPlatformTime::Seconds();
    const FDateTime Utc = FDateTime::UtcNow();
    for (const auto& Entry : FKHULogCapture::Get().Snapshot())
    {
        if (Entry.Time <= LastLiveTime || Entry.Category != FName(TEXT("LogLiveCoding"))) { continue; }
        ApplyLiveMessage(FKHUBuildState::Get().LastCpp, Entry.Text, Utc - FTimespan::FromSeconds(FMath::Max(0.0, Now - Entry.Time)));
        LastLiveTime = FMath::Max(LastLiveTime, Entry.Time);
    }
    return true;
}
FString FKHUCppBuildWatcher::GetErrors(int32 MaxErrors) const
{
    check(IsInGameThread()); const auto& State = FKHUBuildState::Get().LastCpp;
    FString Result = State.IsSet() ? FKHUUbtLogParser::Describe(State.GetValue(), MaxErrors)
        : TEXT("CPP unknown | no verified project build observed; sensor polls every 5s");
    if (!ReadIssue.IsEmpty()) { Result += TEXT("\nSENSOR_WARNING ") + ReadIssue; }
    return FKHULogDigest::Bound(Result);
}

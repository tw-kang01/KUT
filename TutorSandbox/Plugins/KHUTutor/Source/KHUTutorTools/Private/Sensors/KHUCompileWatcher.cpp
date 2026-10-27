#include "Sensors/KHUCompileWatcher.h"
#include "Digest/KHUNodeRefCache.h"
#include "Sensors/KHUCppBuildWatcher.h"
#include "Digest/KHULogDigest.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Logging/TokenizedMessage.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/PlatformTime.h"
#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

namespace
{
    struct FNodeMessage { FString Key; FString Text; bool bError = false; };
    FString StatusName(UBlueprint* Blueprint)
    {
        switch (Blueprint->Status)
        {
        case BS_Error: return TEXT("ERROR");
        case BS_UpToDate: return TEXT("OK");
        case BS_UpToDateWithWarnings: return TEXT("WARN");
        case BS_Dirty: return TEXT("DIRTY");
        case BS_BeingCreated: return TEXT("CREATING");
        default: return TEXT("UNKNOWN");
        }
    }
    TArray<FNodeMessage> Messages(UBlueprint* Blueprint, int32& Errors, int32& Warnings)
    {
        TArray<FNodeMessage> Result;
        TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph) { continue; }
            TArray<UEdGraphNode*> Nodes; FString Issue;
            if (!FKHUNodeRefCache::SortedNodes(Graph, Nodes, Issue)) { Nodes.Reset(); for (UEdGraphNode* Node : Graph->Nodes) { if (Node) { Nodes.Add(Node); } } }
            for (int32 Index = 0; Index < Nodes.Num(); ++Index)
            {
                UEdGraphNode* Node = Nodes[Index];
                if (!Node || !Node->bHasCompilerMessage || Node->ErrorMsg.IsEmpty()) { continue; }
                const bool bError = Node->ErrorType == EMessageSeverity::Error;
                const bool bWarning = Node->ErrorType == EMessageSeverity::Warning || Node->ErrorType == EMessageSeverity::PerformanceWarning;
                if (!bError && !bWarning) { continue; }
                Errors += bError ? 1 : 0; Warnings += bWarning ? 1 : 0;
                const FString Ref = Issue.IsEmpty() ? FString::Printf(TEXT("#%d"), Index + 1) : FString(TEXT("unresolved"));
                FNodeMessage Message;
                Message.Key = Graph->GetPathName() + Node->NodeGuid.ToString();
                Message.bError = bError;
                Message.Text = FString::Printf(TEXT("%s %s %s: \"%s\" graph=%s ref=%s:%s"), bError ? TEXT("E") : TEXT("W"), *Ref,
                    *FKHULogDigest::Sanitize(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()),
                    *FKHULogDigest::Sanitize(Node->ErrorMsg), *FKHULogDigest::Sanitize(Graph->GetName()), *FKHULogDigest::Sanitize(Graph->GetName()), *Ref);
                Result.Add(MoveTemp(Message));
            }
        }
        Result.Sort([](const FNodeMessage& A, const FNodeMessage& B)
        { return A.bError != B.bError ? A.bError : A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0; });
        return Result;
    }
}
FKHUBuildState& FKHUBuildState::Get() { check(IsInGameThread()); static FKHUBuildState State; return State; }
FKHUCompileWatcher& FKHUCompileWatcher::Get() { static FKHUCompileWatcher Watcher; return Watcher; }
void FKHUCompileWatcher::Start()
{
    check(IsInGameThread()); if (bStarted) { return; } bStarted = true;
    if (GEditor) { PreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(this, &FKHUCompileWatcher::BeforeCompile); }
    ModuleHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FKHUCompileWatcher::ModulesChanged);
    AttachLiveCoding();
}
void FKHUCompileWatcher::BeforeCompile(UBlueprint* Blueprint)
{
    check(IsInGameThread()); if (!Blueprint) { return; }
    for (auto It = BlueprintHandles.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
    const TWeakObjectPtr<UBlueprint> Key(Blueprint);
    if (!BlueprintHandles.Contains(Key)) { BlueprintHandles.Add(Key, Blueprint->OnCompiled().AddRaw(this, &FKHUCompileWatcher::AfterCompile)); }
}
void FKHUCompileWatcher::AfterCompile(UBlueprint* Blueprint)
{
    check(IsInGameThread()); if (!bStarted || !Blueprint) { return; }
    FKHUBlueprintCompileState State;
    State.AssetPath = Blueprint->GetPathName(); State.Name = Blueprint->GetName(); State.Status = StatusName(Blueprint);
    State.Time = FPlatformTime::Seconds(); Messages(Blueprint, State.Errors, State.Warnings);
    FKHUBuildState::Get().LastBpCompile = MoveTemp(State); Latest = Blueprint;
}
void FKHUCompileWatcher::AttachLiveCoding()
{
#if WITH_LIVE_CODING
    if (!PatchHandle.IsValid())
    {
        if (auto* Module = FModuleManager::GetModulePtr<ILiveCodingModule>(TEXT("LiveCoding")))
        { PatchHandle = Module->GetOnPatchCompleteDelegate().AddRaw(this, &FKHUCompileWatcher::PatchComplete); }
    }
#endif
}
void FKHUCompileWatcher::ModulesChanged(FName Name, EModuleChangeReason Reason)
{
    if (Name != FName(TEXT("LiveCoding"))) { return; }
    if (Reason == EModuleChangeReason::ModuleLoaded) { AttachLiveCoding(); }
    else if (Reason == EModuleChangeReason::ModuleUnloaded) { PatchHandle.Reset(); }
}
void FKHUCompileWatcher::PatchComplete()
{
    check(IsInGameThread()); FKHUBuildState::Get().LastCppPatchTime = FPlatformTime::Seconds();
    FKHUCppBuildWatcher::Get().PatchComplete();
}
FString FKHUCompileWatcher::LiveCodingStatus() const
{
#if WITH_LIVE_CODING
    if (auto* Module = FModuleManager::GetModulePtr<ILiveCodingModule>(TEXT("LiveCoding")))
    { return Module->IsCompiling() ? TEXT("compiling") : TEXT("idle"); }
#endif
    return TEXT("not_loaded");
}
void FKHUCompileWatcher::Stop()
{
    check(IsInGameThread()); if (!bStarted) { return; } bStarted = false;
    if (GEditor && PreCompileHandle.IsValid()) { GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle); }
    PreCompileHandle.Reset();
    for (const auto& Pair : BlueprintHandles) { if (auto* Blueprint = Pair.Key.Get()) { Blueprint->OnCompiled().Remove(Pair.Value); } }
    BlueprintHandles.Reset();
    Latest.Reset();
    if (ModuleHandle.IsValid()) { FModuleManager::Get().OnModulesChanged().Remove(ModuleHandle); ModuleHandle.Reset(); }
#if WITH_LIVE_CODING
    if (auto* Module = FModuleManager::GetModulePtr<ILiveCodingModule>(TEXT("LiveCoding")))
    { if (PatchHandle.IsValid()) { Module->GetOnPatchCompleteDelegate().Remove(PatchHandle); } }
#endif
    PatchHandle.Reset();
}
FString FKHUCompileWatcher::DescribeBlueprint(UBlueprint* Blueprint)
{
    check(IsInGameThread()); if (!Blueprint) { return TEXT("ERROR: Blueprint is not loaded"); }
    int32 Errors = 0, Warnings = 0; const auto Diagnostics = Messages(Blueprint, Errors, Warnings);
    FString Text = FString::Printf(TEXT("%s [%s, ERROR %d, WARN %d] path=%s"), *FKHULogDigest::Sanitize(Blueprint->GetName()), *StatusName(Blueprint),
        Errors, Warnings, *FKHULogDigest::Sanitize(Blueprint->GetPathName()));
    for (const auto& Message : Diagnostics) { Text += TEXT("\n") + Message.Text; }
    if (Diagnostics.IsEmpty())
    {
        Text += Blueprint->Status == BS_Error ? TEXT("\nNO_NODE_DIAGNOSTICS: compile failed; inspect GetLogDigest") : TEXT("\nNO_NODE_DIAGNOSTICS");
    }
    return FKHULogDigest::Bound(Text);
}
FString FKHUCompileWatcher::GetErrors(const FString& AssetPath)
{
    check(IsInGameThread());
    if (!AssetPath.IsEmpty())
    {
        if (!AssetPath.StartsWith(TEXT("/Game/")) || AssetPath.Contains(TEXT("..")) || AssetPath.Contains(TEXT(":")) || AssetPath.Len() > 512)
        { return TEXT("ERROR: use a loaded /Game/ Blueprint asset path"); }
        FString ObjectPath = AssetPath;
        if (!ObjectPath.Contains(TEXT(".")))
        {
            int32 Slash = INDEX_NONE; ObjectPath.FindLastChar(TEXT('/'), Slash);
            ObjectPath += TEXT(".") + ObjectPath.Mid(Slash + 1);
        }
        return DescribeBlueprint(FindObject<UBlueprint>(nullptr, *ObjectPath));
    }
    TArray<UBlueprint*> Blueprints;
    if (Latest.IsValid()) { Blueprints.Add(Latest.Get()); }
    if (GEditor)
    {
        if (auto* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        { for (UObject* Asset : Subsystem->GetAllEditedAssets()) { if (auto* Blueprint = Cast<UBlueprint>(Asset)) { Blueprints.AddUnique(Blueprint); } } }
    }
    if (Blueprints.IsEmpty()) { return TEXT("BP none | no observed compile or open Blueprint; compile or open the asset first"); }
    FString Text;
    for (UBlueprint* Blueprint : Blueprints)
    {
        if (!Text.IsEmpty()) { Text += TEXT("\n"); }
        Text += DescribeBlueprint(Blueprint);
        if (Text.Len() > 6000) { break; }
    }
    return FKHULogDigest::Bound(Text);
}

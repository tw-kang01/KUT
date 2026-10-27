#include "Toolsets/KHUTutorReadToolset.h"

#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Sensors/KHULogCapture.h"
#include "Sensors/KHUCompileWatcher.h"
#include "Sensors/KHUCppBuildWatcher.h"
#include "Sensors/KHUProjectSource.h"
#include "Sensors/KHUObjectInspector.h"
#include "Engine/Blueprint.h"
#include "Digest/KHULogDigest.h"
#include "Digest/KHUGraphDigest.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformTime.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Selection.h"

DEFINE_LOG_CATEGORY_STATIC(LogKHUTutor, Log, All);

FString UKHUTutorReadToolset::Ping(const FString& Message)
{
    FString Echo = Message.Left(256);
    Echo.ReplaceInline(TEXT("\r"), TEXT(" "));
    Echo.ReplaceInline(TEXT("\n"), TEXT(" "));
    Echo.ReplaceInline(TEXT("|"), TEXT("/"));
    return FString::Printf(
        TEXT("PONG: %s | engine %s | project %s | project_file %s"),
        *Echo,
        *FEngineVersion::Current().ToString(),
        FApp::GetProjectName(),
        *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())).Left(6000);
}

FString UKHUTutorReadToolset::GetTutorContext()
{
    check(IsInGameThread());
    if (!GEditor) { return TEXT("ERROR: editor is not available"); }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    const auto& State = FKHUBuildState::Get();
    FString LastBp = TEXT("none");
    if (State.LastBpCompile.IsSet())
    {
        const auto& Bp = State.LastBpCompile.GetValue();
        LastBp = FString::Printf(TEXT("%s:%s(E%d,W%d)"), *Bp.Name, *Bp.Status, Bp.Errors, Bp.Warnings);
    }
    FString Patch = TEXT("none");
    FString Cpp = TEXT("unknown");
    if (State.LastCpp.IsSet()) { Cpp = State.LastCpp.GetValue().Status + TEXT("(") + State.LastCpp.GetValue().Kind + TEXT(")"); }
    if (State.LastCppPatchTime.IsSet()) { Patch = FString::Printf(TEXT("applied %.0fs ago"), FMath::Max(0.0, FPlatformTime::Seconds() - State.LastCppPatchTime.GetValue())); }
    FString SelectedJson; auto Selected = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Actors;
    if (World)
    {
        for (TActorIterator<AActor> It(World); It && Actors.Num() < 10; ++It)
        {
            if (!GEditor->GetSelectedActors()->IsSelected(*It)) { continue; }
            auto Actor = MakeShared<FJsonObject>();
            Actor->SetStringField(TEXT("label"), FKHULogDigest::Sanitize(It->GetActorLabel(false)).Left(256));
            Actor->SetStringField(TEXT("path"), It->GetPathName()); Actor->SetStringField(TEXT("world"), World->GetPathName()); Actor->SetStringField(TEXT("world_scope"), TEXT("Editor"));
            Actor->SetStringField(TEXT("name"), It->GetName()); Actor->SetStringField(TEXT("class"), It->GetClass()->GetName());
            Actors.Add(MakeShared<FJsonValueObject>(Actor));
        }
    }
    Selected->SetArrayField(TEXT("actors"), Actors);
    FJsonSerializer::Serialize(Selected, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SelectedJson));
    // Editor viewport Realtime: effects, animation and Tick only advance in the level viewport while Realtime is on (or during PIE).
    // Stored state and a temporary override are reported separately; a forced extra frame is not reported as "on".
    FString Realtime = TEXT("none");
    if (const FLevelEditorViewportClient* Viewport = GCurrentLevelEditingViewportClient)
    {
        if (Viewport->DoRealtimeAndOverridesMatch(true)) { Realtime = TEXT("on"); }
        else if (Viewport->DoRealtimeAndOverridesMatch(false)) { Realtime = TEXT("off"); }
        else { Realtime = Viewport->IsRealtime() ? TEXT("on(override)") : TEXT("off(override)"); }
    }
    return FKHULogDigest::Bound(FString::Printf(TEXT("ENGINE %s | EDITOR_LANG %s | PROJECT %s (BP+C++) | LEVEL %s | PIE %s | VIEWPORT_REALTIME %s | SELECTED %d | LAST_BP_COMPILE %s | LAST_CPP_BUILD %s | LIVE_CODING %s | LAST_CPP_PATCH %s\nSELECTED_ACTORS %s"),
        *FEngineVersion::Current().ToString(), *FInternationalization::Get().GetCurrentLanguage()->GetName(),
        *FKHULogDigest::Sanitize(FApp::GetProjectName()), World ? *FKHULogDigest::Sanitize(World->GetPathName()) : TEXT("none"),
        GEditor->PlayWorld ? TEXT("on") : TEXT("off"), *Realtime, GEditor->GetSelectedActorCount(),
        *FKHULogDigest::Sanitize(LastBp), *Cpp, *FKHUCompileWatcher::Get().LiveCodingStatus(), *Patch, *SelectedJson));
}
FString UKHUTutorReadToolset::GetLogDigest(const FString& MinSeverity, int32 SinceSeconds, int32 MaxLines)
{
    FKHULogDigestOptions Options; Options.MinSeverity = MinSeverity; Options.SinceSeconds = SinceSeconds; Options.MaxLines = MaxLines;
    Options.NoiseCategories = {FName(TEXT("LogSlate")), FName(TEXT("LogRenderer")), FName(TEXT("LogRHI")), FName(TEXT("LogShaderCompilers"))};
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("KHUTutor"));
    bool bLoadedConfig = false;
    if (Plugin.IsValid())
    {
        FString Text; TSharedPtr<FJsonObject> Object; const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
        if (FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Config/LogDigest.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid()
            && Object->TryGetArrayField(TEXT("noise_categories"), Names))
        {
            TSet<FName> Categories;
            bLoadedConfig = true;
            for (const auto& Value : *Names)
            {
                FString Name;
                if (!Value.IsValid() || !Value->TryGetString(Name)) { bLoadedConfig = false; break; }
                Categories.Add(FName(*Name));
            }
            if (bLoadedConfig) { Options.NoiseCategories = MoveTemp(Categories); }
        }
    }
    if (!bLoadedConfig)
    { UE_LOG(LogKHUTutor, Warning, TEXT("LogDigest.json is missing or invalid; using default noise categories.")); }
    return FKHULogDigest::Build(FKHULogCapture::Get().Snapshot(), FPlatformTime::Seconds(), Options);
}
FString UKHUTutorReadToolset::GetBlueprintCompileErrors(const FString& AssetPath)
{
    return FKHUCompileWatcher::Get().GetErrors(AssetPath);
}
FString UKHUTutorReadToolset::GetCppBuildErrors(int32 MaxErrors)
{
    return FKHUCppBuildWatcher::Get().GetErrors(MaxErrors);
}
FString UKHUTutorReadToolset::ReadProjectSource(const FString& RelativePath, int32 Line, int32 Radius)
{
    return FKHUProjectSource::Read(FPaths::ProjectDir(), RelativePath, Line, Radius);
}
FString UKHUTutorReadToolset::GetBlueprintGraphDigest(const FString& AssetPath, const FString& GraphName, const FString& Focus, int32 MaxNodes, int32 Hops)
{
    check(IsInGameThread()); FString Issue;
    UBlueprint* Blueprint = FKHUGraphDigest::FindLoadedBlueprint(AssetPath, Issue);
    if (!Blueprint) { return TEXT("ERROR: ") + Issue; }
    UEdGraph* Graph = FKHUGraphDigest::ChooseGraph(Blueprint, GraphName);
    if (!Graph) { return TEXT("ERROR: graph not found; use an exact graph name"); }
    FKHUGraphDigestOptions Options; Options.Focus = Focus; Options.MaxNodes = MaxNodes; Options.Hops = Hops;
    if (Focus == TEXT("selection")) { Options.Selection = FKHUGraphDigest::SelectedNodes(Blueprint, Graph); }
    return FKHUGraphDigest::Build(Blueprint, Graph, MoveTemp(Options));
}

FString UKHUTutorReadToolset::GetActorRelations(const FString& ActorPath, const FString& WorldScope, int32 Depth, int32 MaxObjects)
{
    check(IsInGameThread()); FString Issue; UWorld* World = FKHUObjectInspector::World(WorldScope, Issue);
    if (!World) { return TEXT("ERROR: ") + Issue; }
    TArray<UObject*> Roots;
    if (!ActorPath.IsEmpty())
    {
        UObject* Object = FKHUObjectInspector::Find(World, ActorPath, Issue);
        if (!Object || !Object->IsA<AActor>()) { return TEXT("ERROR: use an exact actor path in the requested world. ") + Issue; }
        Roots.Add(Object);
    }
    else if (GEditor)
    {
        for (TActorIterator<AActor> It(World); It && Roots.Num() < 8; ++It)
        { if (GEditor->GetSelectedActors()->IsSelected(*It)) { Roots.Add(*It); } }
    }
    return FKHUObjectInspector::Relations(World, Roots, Depth, MaxObjects);
}
FString UKHUTutorReadToolset::GetObjectDetails(const FString& ObjectPath, const FString& WorldScope, const FString& PropertyNames, int32 MaxProperties)
{
    check(IsInGameThread()); FString Issue;
    if (WorldScope == TEXT("ClassDefault"))
    {
        UBlueprint* BP = FKHUGraphDigest::FindLoadedBlueprint(ObjectPath, Issue);
        if (!BP || !BP->GeneratedClass) { return TEXT("ERROR: loaded Blueprint generated class unavailable. ") + Issue; }
        return FKHUObjectInspector::Details(BP->GeneratedClass->GetDefaultObject(false), nullptr, PropertyNames, MaxProperties);
    }
    UWorld* World = FKHUObjectInspector::World(WorldScope, Issue);
    if (!World) { return TEXT("ERROR: ") + Issue; }
    UObject* Object = FKHUObjectInspector::Find(World, ObjectPath, Issue);
    return Object ? FKHUObjectInspector::Details(Object, World, PropertyNames, MaxProperties) : TEXT("ERROR: ") + Issue;
}

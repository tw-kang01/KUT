#include "Toolsets/KHUTutorShowToolset.h"
#include "Digest/KHUGraphDigest.h"
#include "Digest/KHUNodeRefCache.h"
#include "Digest/KHULogDigest.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraphNode.h"
#include "Selection.h"
#include "ScopedTransaction.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "CoreGlobals.h"
#include "Misc/App.h"

namespace
{
    bool CanNavigate() { return GEditor && !IsRunningCommandlet() && FApp::CanEverRender() && FSlateApplication::IsInitialized(); }
}
FString UKHUTutorShowToolset::FocusActor(const FString& ActorLabel)
{
    check(IsInGameThread());
    if (ActorLabel.IsEmpty() || ActorLabel.Len() > 1024) { return TEXT("ERROR: use an exact actor path, label or name"); }
    if (!CanNavigate()) { return TEXT("ERROR: Show tools require a graphical editor"); }
    if (GEditor->PlayWorld) { return TEXT("ERROR: stop PIE before focusing an editor actor"); }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) { return TEXT("ERROR: editor level is unavailable"); }
    AActor* Found = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (ActorLabel.StartsWith(TEXT("/")) ? It->GetPathName() == ActorLabel : (It->GetActorLabel(false).Equals(ActorLabel, ESearchCase::CaseSensitive) || It->GetName().Equals(ActorLabel, ESearchCase::CaseSensitive)))
        { if (Found) { return TEXT("ERROR: ambiguous actor label; use the unique actor name"); } Found = *It; }
    }
    if (!Found) { return TEXT("ERROR: actor not found in the editor level"); }
    if (!GEditor->CanSelectActor(Found, true, false, false)) { return TEXT("ERROR: actor cannot be selected"); }
    {
        const FScopedTransaction Transaction(NSLOCTEXT("KHUTutor", "FocusActor", "KHU Tutor: 액터 보기"));
        GEditor->GetSelectedActors()->Modify(false);
        GEditor->SelectNone(false, false, false); GEditor->SelectActor(Found, true, true);
    }
    if (!GEditor->GetSelectedActors()->IsSelected(Found)) { return TEXT("ERROR: actor selection failed"); }
    GEditor->MoveViewportCamerasToActor(*Found, true);
    return TEXT("OK: focused actor ") + FKHULogDigest::Sanitize(Found->GetActorLabel(false));
}
FString UKHUTutorShowToolset::OpenAssetEditor(const FString& AssetPath)
{
    check(IsInGameThread()); FString Path;
    if (!FKHUGraphDigest::ObjectPath(AssetPath, Path)) { return TEXT("ERROR: use a /Game/ asset path"); }
    if (!CanNavigate()) { return TEXT("ERROR: Show tools require a graphical editor"); }
    if (GEditor->PlayWorld) { return TEXT("ERROR: stop PIE before opening an asset editor"); }
    UObject* Asset = FindObject<UObject>(nullptr, *Path);
    if (!Asset) { Asset = LoadObject<UObject>(nullptr, *Path); }
    if (!Asset) { return TEXT("ERROR: asset not found"); }
    auto* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!Subsystem || !Subsystem->OpenEditorForAsset(Asset) || !Subsystem->FindEditorForAsset(Asset, false))
    { return TEXT("ERROR: asset editor could not be opened"); }
    return TEXT("OK: opened asset editor ") + FKHULogDigest::Sanitize(Path);
}
FString UKHUTutorShowToolset::FocusGraphNode(const FString& AssetPath, const FString& NodeRef)
{
    check(IsInGameThread()); FString Issue;
    UBlueprint* Blueprint = FKHUGraphDigest::FindLoadedBlueprint(AssetPath, Issue);
    if (!Blueprint) { return TEXT("ERROR: ") + Issue; }
    UEdGraphNode* Node = FKHUNodeRefCache::Get().Resolve(Blueprint, NodeRef, Issue);
    if (!Node) { return TEXT("ERROR: ") + Issue; }
    if (!CanNavigate()) { return TEXT("ERROR: Show tools require a graphical editor"); }
    if (GEditor->PlayWorld) { return TEXT("ERROR: stop PIE before focusing a Blueprint node"); }
    const auto Editor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, true);
    if (!Editor.IsValid()) { return TEXT("ERROR: Blueprint editor could not be opened"); }
    FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
    if (Editor->GetFocusedGraph() != Node->GetGraph()) { return TEXT("ERROR: graph focus was not applied"); }
    return TEXT("OK: focused node ") + NodeRef + TEXT(" in ") + FKHULogDigest::Sanitize(Node->GetGraph()->GetName());
}

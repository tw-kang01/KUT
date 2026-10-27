#include "UI/KHUEditorObservation.h"
#include "Digest/KHUGraphDigest.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "GraphEditAction.h"
#include "Mcp/KHUMcpProtocol.h"

FKHUEditorObservation::FKHUEditorObservation(UBlueprint* Blueprint, UEdGraph* InGraph, bool bSelection, const TArray<UEdGraphNode*>& Selected, const FString& CapturedDigest)
    : bSelectionOnly(bSelection), Graph(InGraph)
{
    if (!Blueprint || !InGraph || InGraph->GetTypedOuter<UBlueprint>() != Blueprint)
    { Error = TEXT("대상 Blueprint의 Graph를 먼저 열어 주세요."); return; }
    AssetPath = Blueprint->GetPathName(); GraphName = InGraph->GetName(); ReadAt = FDateTime::UtcNow().ToIso8601();
    FKHUGraphDigestOptions Options; Options.Focus = bSelection ? TEXT("selection") : TEXT("all");
    Options.MaxNodes = 60; Options.Hops = 0;
    for (auto* Node : Selected) { if (Node && Node->GetGraph() == InGraph) { Options.Selection.AddUnique(Node); } }
    SelectedCount = Options.Selection.Num();
    if (bSelection && SelectedCount == 0) { Error = TEXT("선택한 노드가 없습니다. 노드를 선택하거나 ‘현재 Graph 요약’을 사용해 주세요."); return; }
    Digest = CapturedDigest.IsEmpty() ? FKHUGraphDigest::Build(Blueprint, InGraph, MoveTemp(Options)) : CapturedDigest;
    if (Digest.StartsWith(TEXT("ERROR:"))) { Error = TEXT("그래프를 읽지 못했습니다: ") + Digest; return; }
    ChangeHandle = InGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateLambda([this](const FEdGraphEditAction& Action)
    { if (Action.Action != GRAPHACTION_SelectNode) { bChanged = true; } }));
}
FKHUEditorObservation::~FKHUEditorObservation()
{
    if (auto* G = Graph.Get()) { if (ChangeHandle.IsValid()) { G->RemoveOnGraphChangedHandler(ChangeHandle); } }
}
bool FKHUEditorObservation::IsCurrent() const { return Error.IsEmpty() && Graph.IsValid() && !bChanged; }
FString FKHUEditorObservation::Json() const
{
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("asset"), AssetPath); O->SetStringField(TEXT("graph"), GraphName);
    O->SetStringField(TEXT("world"), TEXT("Editor asset, not a PIE instance"));
    O->SetStringField(TEXT("read_at_utc"), ReadAt);
    O->SetStringField(TEXT("scope"), bSelectionOnly ? TEXT("selected nodes, zero expansion hops") : TEXT("current graph summary, at most 60 nodes; text may be truncated"));
    O->SetStringField(TEXT("graph_digest"), Digest); O->SetBoolField(TEXT("current"), IsCurrent());
    return KHUMcpProtocol::WriteObject(O);
}

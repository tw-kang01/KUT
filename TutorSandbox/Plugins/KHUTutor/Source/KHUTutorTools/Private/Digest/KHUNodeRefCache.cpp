#include "Digest/KHUNodeRefCache.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

FKHUNodeRefCache& FKHUNodeRefCache::Get() { check(IsInGameThread()); static FKHUNodeRefCache Cache; return Cache; }
bool FKHUNodeRefCache::SortedNodes(UEdGraph* Graph, TArray<UEdGraphNode*>& Nodes, FString& Issue)
{
    Nodes.Reset(); Issue.Reset();
    if (!Graph || Graph->Nodes.Num() > 4096) { Issue = TEXT("graph unavailable or exceeds 4096 nodes"); return false; }
    TSet<FGuid> Guids;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node) { continue; }
        if (!Node->NodeGuid.IsValid() || Guids.Contains(Node->NodeGuid)) { Issue = TEXT("graph has missing or duplicate node GUIDs"); return false; }
        Guids.Add(Node->NodeGuid); Nodes.Add(Node);
    }
    Nodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
    { return A.NodeGuid.ToString(EGuidFormats::Digits).Compare(B.NodeGuid.ToString(EGuidFormats::Digits), ESearchCase::CaseSensitive) < 0; });
    return true;
}
void FKHUNodeRefCache::Publish(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes)
{
    check(IsInGameThread());
    const FString Asset = Blueprint->GetPathName(), Name = Graph->GetName();
    Snapshots.RemoveAll([&](const FSnapshot& S) { return !S.Graph.IsValid() || S.Graph.Get() == Graph; });
    if (Snapshots.Num() >= 32) { Snapshots.RemoveAt(0); }
    FSnapshot Next; Next.Asset = Asset; Next.GraphName = Name; Next.Graph = Graph;
    for (UEdGraphNode* Node : Nodes) { Next.Nodes.Add(Node); Next.Guids.Add(Node->NodeGuid); }
    Snapshots.Add(MoveTemp(Next));
}
UEdGraphNode* FKHUNodeRefCache::Resolve(UBlueprint* Blueprint, const FString& Reference, FString& Issue) const
{
    check(IsInGameThread());
    if (!Blueprint || Reference.IsEmpty() || Reference.Len() > 128) { Issue = TEXT("invalid Blueprint or node reference"); return nullptr; }
    FString Number = Reference, GraphName;
    Reference.Split(TEXT(":"), &GraphName, &Number, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (Number.StartsWith(TEXT("#")))
    {
        if (!GraphName.IsEmpty())
        {
            TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs); UEdGraph* NamedGraph = nullptr;
            for (UEdGraph* Graph : Graphs)
            {
                if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::CaseSensitive))
                { if (NamedGraph && NamedGraph != Graph) { Issue = TEXT("ambiguous graph name; read a fresh digest and use #n or an exact node GUID"); return nullptr; } NamedGraph = Graph; }
            }
        }
        const FString Digits = Number.Mid(1);
        if (Digits.IsEmpty() || Digits.Len() > 4) { Issue = TEXT("node number must be #1..#4096"); return nullptr; }
        for (TCHAR Ch : Digits) { if (Ch < TEXT('0') || Ch > TEXT('9')) { Issue = TEXT("invalid node number"); return nullptr; } }
        const int32 Index = FCString::Atoi(*Digits) - 1;
        const FSnapshot* Snapshot = nullptr;
        for (int32 I = Snapshots.Num() - 1; I >= 0; --I)
        {
            const auto& S = Snapshots[I];
            if (S.Asset == Blueprint->GetPathName() && (GraphName.IsEmpty() || S.GraphName == GraphName)) { Snapshot = &S; break; }
        }
        if (!Snapshot || !Snapshot->Graph.IsValid()) { Issue = TEXT("read the graph digest before using #n"); return nullptr; }
        TArray<UEdGraphNode*> Current;
        if (!SortedNodes(Snapshot->Graph.Get(), Current, Issue)) { return nullptr; }
        if (Current.Num() != Snapshot->Nodes.Num()) { Issue = TEXT("graph changed; read a fresh digest"); return nullptr; }
        for (int32 I = 0; I < Current.Num(); ++I)
        {
            if (Current[I] != Snapshot->Nodes[I].Get() || Current[I]->NodeGuid != Snapshot->Guids[I])
            { Issue = TEXT("graph changed; read a fresh digest"); return nullptr; }
        }
        if (!Current.IsValidIndex(Index)) { Issue = TEXT("node number is outside the digest"); return nullptr; }
        return Current[Index];
    }
    FGuid Guid;
    if (!GraphName.IsEmpty() || !FGuid::Parse(Reference, Guid) || !Guid.IsValid()) { Issue = TEXT("use #n, GraphName:#n, or a node GUID"); return nullptr; }
    TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
    UEdGraphNode* Found = nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph) { continue; }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == Guid)
            { if (Found) { Issue = TEXT("ambiguous duplicate node GUID"); return nullptr; } Found = Node; }
        }
    }
    if (!Found) { Issue = TEXT("node GUID not found in this Blueprint"); }
    return Found;
}
void FKHUNodeRefCache::Reset() { check(IsInGameThread()); Snapshots.Reset(); }

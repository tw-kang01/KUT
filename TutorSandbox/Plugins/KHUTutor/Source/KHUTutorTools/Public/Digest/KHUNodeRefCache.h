#pragma once
#include "CoreMinimal.h"
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/** Session-only, weak graph snapshots. Never retarget an old #n after graph edits. */
class KHUTUTORTOOLS_API FKHUNodeRefCache
{
public:
    /** Return this module's game-thread cache. */
    static FKHUNodeRefCache& Get();
    /** Sort valid, unique node GUIDs; fail explicitly on malformed/oversized graphs. */
    static bool SortedNodes(UEdGraph* Graph, TArray<UEdGraphNode*>& Nodes, FString& Issue);
    /** Publish the numbering used by a digest, keeping at most 32 weak snapshots. */
    void Publish(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes);
    /** Resolve #n in the last digest, GraphName:#n, or an exact node GUID. */
    UEdGraphNode* Resolve(UBlueprint* Blueprint, const FString& Reference, FString& Issue) const;
    /** Clear snapshots before module unload. */
    void Reset();
private:
    struct FSnapshot
    {
        FString Asset;
        FString GraphName;
        TWeakObjectPtr<UEdGraph> Graph;
        TArray<TWeakObjectPtr<UEdGraphNode>> Nodes;
        TArray<FGuid> Guids;
    };
    TArray<FSnapshot> Snapshots;
};

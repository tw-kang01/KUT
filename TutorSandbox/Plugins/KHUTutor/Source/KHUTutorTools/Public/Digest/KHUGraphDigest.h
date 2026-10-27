#pragma once
#include "CoreMinimal.h"
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

struct FKHUGraphDigestOptions
{
    FString Focus = TEXT("errors");
    int32 MaxNodes = 20;
    int32 Hops = 2;
    TArray<UEdGraphNode*> Selection;
};

/** Read-only graph traversal and compact, bounded text. */
class KHUTUTORTOOLS_API FKHUGraphDigest
{
public:
    /** Validate /Game/ package/object paths without loading or executing anything. */
    static bool ObjectPath(const FString& AssetPath, FString& Path);
    /** Find an already loaded /Game/ Blueprint; never load or compile it. */
    static UBlueprint* FindLoadedBlueprint(const FString& AssetPath, FString& Issue);
    /** Choose an exact graph name, otherwise the first diagnostic graph or EventGraph. */
    static UEdGraph* ChooseGraph(UBlueprint* Blueprint, const FString& GraphName);
    /** Read selected nodes in an existing UE Blueprint editor without opening it. */
    static TArray<UEdGraphNode*> SelectedNodes(UBlueprint* Blueprint, UEdGraph* Graph);
    /** Format one graph, preserve seed diagnostics and publish the exact GUID numbering. */
    static FString Build(UBlueprint* Blueprint, UEdGraph* Graph, FKHUGraphDigestOptions Options);
    /** Recognize an error/warning diagnostic, excluding informational messages. */
    static bool HasDiagnostic(const UEdGraphNode* Node);
};

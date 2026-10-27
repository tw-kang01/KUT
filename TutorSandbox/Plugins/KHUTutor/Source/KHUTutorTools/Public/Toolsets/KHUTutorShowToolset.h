#pragma once
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "KHUTutorShowToolset.generated.h"

/** Presentation-only navigation. Does not alter assets or actor properties. */
UCLASS()
class KHUTUTORTOOLS_API UKHUTutorShowToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** Select one actor by exact object path (preferred), or unique label/name, in the editor level and frame it.
     * Rejects ambiguity, PIE, and headless editors. No actor property changes.
     */
    UFUNCTION(meta=(AICallable))
    static FString FocusActor(const FString& ActorLabel);
    /** Open a /Game/ asset in its editor. Explicit Show request may load the asset.
     * No compiling, saving, or asset edits. Requires a graphical editor.
     */
    UFUNCTION(meta=(AICallable))
    static FString OpenAssetEditor(const FString& AssetPath);
    /** Focus #n from the last digest, GraphName:#n, or an exact NodeGuid in a Blueprint.
     * Rejects stale numbering. Opens the editor without changing the graph.
     */
    UFUNCTION(meta=(AICallable))
    static FString FocusGraphNode(const FString& AssetPath, const FString& NodeRef);
};

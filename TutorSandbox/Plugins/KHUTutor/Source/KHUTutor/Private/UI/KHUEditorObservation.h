#pragma once
#include "CoreMinimal.h"
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/** Immutable read evidence, invalidated by graph edits. Never compiles or changes assets. */
class FKHUEditorObservation
{
public:
    FKHUEditorObservation(UBlueprint* Blueprint, UEdGraph* Graph, bool bSelection, const TArray<UEdGraphNode*>& Selected, const FString& CapturedDigest = FString());
    ~FKHUEditorObservation();
    FKHUEditorObservation(const FKHUEditorObservation&) = delete;
    FKHUEditorObservation& operator=(const FKHUEditorObservation&) = delete;
    /** False for missing, edited or failed evidence. */
    bool IsCurrent() const;
    /** JSON data, not instructions; contains only editor-time observations. */
    FString Json() const;
    FString AssetPath, GraphName, Digest, ReadAt, Error;
    bool bSelectionOnly = false;
    int32 SelectedCount = 0;
    TWeakObjectPtr<UEdGraph> Graph;
private:
    FDelegateHandle ChangeHandle;
    bool bChanged = false;
};

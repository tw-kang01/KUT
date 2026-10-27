#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "KHUTutorReadToolset.generated.h"

/** Read-only KHU tutor tools. */
UCLASS()
class KHUTUTORTOOLS_API UKHUTutorReadToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Echo a short Message and identify this editor's engine and project. No changes.
     * Returns PONG: <message> | engine <version> | project <name> | project_file <path>.
     * Message is limited to 256 characters; output is limited to 6000 characters.
     */
    UFUNCTION(meta=(AICallable))
    static FString Ping(const FString& Message);
    /** Summarize the current editor, level, PIE, viewport Realtime, selection and observed compile state.
     * VIEWPORT_REALTIME on/off tells whether effects, animation and Tick advance in the level viewport without PIE.
     * SELECTED_ACTORS[].path is the exact ActorPath for GetActorRelations. Read only; no loading or compilation; <=6000 characters.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetTutorContext();
    /** Read recent logs, group repeats and include context around the first error.
     * MinSeverity: Fatal/Error/Warning/Display/Log. Masks keys and user paths; <=6000 characters.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetLogDigest(const FString& MinSeverity = TEXT("Warning"), int32 SinceSeconds = 600, int32 MaxLines = 40);
    /** Read node diagnostics for a loaded /Game/ Blueprint, or latest compile/open Blueprints.
     * AssetPath may be a package or object path. No loading/compiling; <=6000 characters.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetBlueprintCompileErrors(const FString& AssetPath = TEXT(""));
    /** Read the latest verified project UBT/Live Coding outcome and compiler errors.
     * MaxErrors is clamped to 1..50; no compilation. Output <=6000 characters.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetCppBuildErrors(int32 MaxErrors = 10);
    /** Read a numbered excerpt from a project-relative Source/ C++ or Build.cs file.
     * Line is 1-based, Radius is 0..40. Rejects traversal/absolute paths and resolved escapes.
     * Masks sensitive text; <=6000 characters. No writing or execution.
     */
    UFUNCTION(meta=(AICallable))
    static FString ReadProjectSource(const FString& RelativePath, int32 Line = 1, int32 Radius = 20);
    /** Read a loaded Blueprint graph with stable #n, pins, diagnostics, variables and comments.
     * Supply GraphName (empty selects a diagnostic graph or EventGraph). Focus: errors/selection/all.
     * MaxNodes 1..60, Hops 0..4. No loading/compilation; output <=6000 characters.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetBlueprintGraphDigest(const FString& AssetPath, const FString& GraphName = TEXT(""), const FString& Focus = TEXT("errors"), int32 MaxNodes = 20, int32 Hops = 2);
    /** Read bounded actor/component references in exactly Editor or PIE world. Empty ActorPath uses selected actors in that world.
     * ActorPath must be an exact loaded object path, never a label. Depth 0..2, MaxObjects 1..8.
     * No loading/mutation. Output JSON <=6000 chars, with world/time and explicit omissions. None alone is not a bug.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetActorRelations(const FString& ActorPath = TEXT(""), const FString& WorldScope = TEXT("Editor"), int32 Depth = 1, int32 MaxObjects = 4);
    /** Read editable/Blueprint-visible scalar values and compare with class/archetype defaults. No getters or mutation.
     * ObjectPath: exact loaded actor/component path for Editor/PIE, or loaded /Game/ Blueprint path for ClassDefault.
     * PropertyNames: comma-separated exact reflected names, empty selects a bounded list. MaxProperties 1..16.
     * JSON <=6000 chars. Unsupported values are not_read, not None. A differing value does not prove a saved override.
     */
    UFUNCTION(meta=(AICallable))
    static FString GetObjectDetails(const FString& ObjectPath, const FString& WorldScope = TEXT("Editor"), const FString& PropertyNames = TEXT(""), int32 MaxProperties = 12);
};

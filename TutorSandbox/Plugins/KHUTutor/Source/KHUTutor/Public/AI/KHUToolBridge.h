#pragma once
#include "AI/KHUConfig.h"

/** Stable, immutable whitelist projection of native schemas into Chat Completions tools. */
class KHUTUTOR_API FKHUToolBridge
{
public:
    /** Build a catalog; unresolved references and duplicate names are rejected. */
    FKHUToolBridge(const FKHUConfig& Config, const TArray<TSharedPtr<FJsonValue>>& NativeTools);
    /** Cache definitions by tier, hint level and explicit Show permission. */
    const TArray<TSharedPtr<FJsonValue>>& Definitions(const FString& Tier, int32 Hint, bool bAllowShow = false);
    /** Serialize each policy's immutable tool array only once. */
    const FString& SerializedDefinitions(const FString& Tier, int32 Hint, bool bAllowShow = false);
    /** Resolve an exact function name and validate arguments before any execution. */
    bool Resolve(const FString& Function, const FString& Json, const FString& Tier, int32 Hint, bool bAllowShow,
        FString& Native, TSharedPtr<FJsonObject>& Arguments, FString& Error) const;
    /** Deterministic short name suitable for OpenAI-compatible function calling. */
    static FString FunctionName(const FString& Native);
    /** Keep both ends of an oversized result within an estimated token budget. */
    static FString Compress(const FString& Text, int32 MaxTokens);
    /** Recursive argument validation; keys and required names are case-sensitive. */
    static bool Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value, int32 Depth = 0);
private:
    struct FTool { FString Native, Function, Group; TSharedPtr<FJsonObject> Schema, Definition; };
    bool Allowed(const FTool& Tool, const FString& Tier, int32 Hint, bool bAllowShow) const;
    TArray<FTool> Tools;
    TMap<FString, TArray<TSharedPtr<FJsonValue>>> Cache;
    TMap<FString, FString> SerializedCache;
    TArray<FString> Never;
};

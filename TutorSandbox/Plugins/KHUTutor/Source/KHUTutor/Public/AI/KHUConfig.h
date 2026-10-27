#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct KHUTUTOR_API FKHUModelTier
{
    TArray<FString> Chain;
    int32 MaxIters = 0, InputBudget = 0;
    TOptional<double> Temperature;
    FString ReasoningEffort;
    /** Optional per-tier visible-output cap; 0 keeps the hint policy. A model that writes long answers needs headroom or the CLI continues in a second turn and drops the first part. */
    int32 OutputTokens = 0;
    FString Api = TEXT("chat_completions");
};
struct KHUTUTOR_API FKHUPrice
{
    double Input = 0, Output = 0, CachedInput = 0;
    double CacheWrite = 0, CacheWriteHour = 0;
    TOptional<double> CreditsPerUsd;
};
/** Token estimates and wire byte limits are independent, configurable bounds. */
struct KHUTUTOR_API FKHUContextPolicy
{
    double TokensPerByte = 0, MinRatio = 0, MaxRatio = 0, Safety = 0, Alpha = 0;
    int32 MaxBytes = 0, MaxMessages = 0, RecentTurns = 0, TokenOverhead = 0;
    bool bProjectMemory = false;
    int32 SessionTurns = 0, RotateTokens = 0, MemoryChars = 0, ProjectEntries = 0;
    int32 ArchiveMaxBytes = 0, ArchiveMaxTurns = 0;
};
/** Configuration fails closed: invalid files never enable a paid request. */
struct KHUTUTOR_API FKHUConfig
{
    TMap<FString, FKHUModelTier> Tiers;
    TMap<FString, FKHUPrice> Prices;
    TMap<FString, TArray<FString>> ToolGroups;
    TArray<FString> Never, DisabledModels, Warnings;
    FString BaseUrl, SystemPrompt, FixtureDirectory;
    /** Prompts/l0_contract.txt: the visible-answer rule appended to every L0 request after the student message. */
    FString L0Contract;
    TSharedPtr<FJsonObject> ErrorPatterns;
    double PerTaskCredits = 0, PerTaskHardCredits = 0, SemesterCredits = 0, DevelopmentRemaining = 0;
    FKHUContextPolicy Context;
    int32 MaxOutputTokens = 0, OutputHardLimit = 0, ResultMaxTokens = 0;
    float TimeoutSeconds = 0;
    bool bValid = false;
    bool bDevelopmentUnlimited = false;
    TArray<FString> DevelopmentModels;
    bool bClaudePanel = false;
    FString ClaudeTier, ClaudeExecutable;
    /** "5m" or "1h"; empty keeps the CLI default. Longer TTL costs more per write but survives a student's pauses. */
    FString ClaudePromptCacheTtl;
    int32 ClaudeTimeout = 180, ClaudeContextTokens = 20000;
    static FKHUConfig Load(const FString& ConfigDirectory);
    bool IsModelEnabled(const FString& Model, bool bDevelopment = false) const;
    TOptional<double> Cost(const FString& Model, int64 Input, int64 Cached, int64 Output) const;
};

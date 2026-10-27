#include "AI/KHUConfig.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    TSharedPtr<FJsonObject> ReadConfig(const FString& Directory, const FString& Name, TArray<FString>& Warnings)
    {
        FString Text; TSharedPtr<FJsonObject> Object;
        if (!FFileHelper::LoadFileToString(Text, *(Directory / Name)) || Text.Len() > 1024 * 1024 || !KHUMcpProtocol::ReadObject(Text, Object))
        { Warnings.Add(Name + TEXT(": missing or invalid JSON; paid requests disabled")); return nullptr; }
        return Object;
    }
    bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double& Value)
    {
        double Candidate = 0;
        if (Object.IsValid() && Object->TryGetNumberField(Name, Candidate) && FMath::IsFinite(Candidate) && Candidate >= 0) { Value = Candidate; return true; }
        return false;
    }
    TArray<FString> Strings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
    {
        TArray<FString> Result; const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Object.IsValid() && Object->TryGetArrayField(Name, Array))
        { for (const auto& V : *Array) { FString S; if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) { Result.AddUnique(S); } } }
        return Result;
    }
}

FKHUConfig FKHUConfig::Load(const FString& Directory)
{
    FKHUConfig C;
    const auto Models = ReadConfig(Directory, TEXT("Models.json"), C.Warnings);
    const auto Pricing = ReadConfig(Directory, TEXT("Pricing.json"), C.Warnings);
    const auto Budget = ReadConfig(Directory, TEXT("Budget.json"), C.Warnings);
    const auto Tools = ReadConfig(Directory, TEXT("ToolWhitelist.json"), C.Warnings);
    const auto Gateway = ReadConfig(Directory, TEXT("Gateway.json"), C.Warnings);
    const auto Development = ReadConfig(Directory, TEXT("DevelopmentBudget.json"), C.Warnings);
    const auto Context = ReadConfig(Directory, TEXT("Context.json"), C.Warnings);
    C.ErrorPatterns = ReadConfig(Directory, TEXT("ErrorPatterns.json"), C.Warnings);
    const TSharedPtr<FJsonObject>* Tiers = nullptr;
    if (Models.IsValid() && Models->TryGetObjectField(TEXT("tiers"), Tiers))
    {
        C.DisabledModels = Strings(Models, TEXT("disabled_patterns"));
        for (const auto& Pair : (*Tiers)->Values)
        {
            if (Pair.Value->Type != EJson::Object) { continue; }
            const auto Tier = Pair.Value->AsObject(); FKHUModelTier T; double Iters = 0, Input = 0, Temperature = 0;
            T.Chain = Strings(Tier, TEXT("chain"));
            if (!Number(Tier, TEXT("max_iters"), Iters) || Iters < 1 || Iters > 20 || FMath::FloorToDouble(Iters) != Iters
                || !Number(Tier, TEXT("input_budget"), Input) || Input < 1 || Input > 100000 || FMath::FloorToDouble(Input) != Input) { continue; }
            T.MaxIters = static_cast<int32>(Iters); T.InputBudget = static_cast<int32>(Input);
            if (Number(Tier, TEXT("temperature"), Temperature) && Temperature <= 2) { T.Temperature = Temperature; }
            Tier->TryGetStringField(TEXT("reasoning_effort"), T.ReasoningEffort);
            double OutputValue = 0; if (Tier->HasField(TEXT("output_tokens"))) { if (!Number(Tier, TEXT("output_tokens"), OutputValue) || OutputValue < 1 || OutputValue > 32000 || FMath::FloorToDouble(OutputValue) != OutputValue) { continue; } T.OutputTokens = static_cast<int32>(OutputValue); }
            Tier->TryGetStringField(TEXT("api"), T.Api);
            if (T.Api != TEXT("chat_completions") && T.Api != TEXT("responses")) { continue; }
            if (!T.ReasoningEffort.IsEmpty()) { T.Temperature.Reset(); }
            C.Tiers.Add(FString(*Pair.Key), MoveTemp(T));
        }
    }
    FString Unit; const TSharedPtr<FJsonObject>* Prices = nullptr; double GlobalConversion = 0;
    const bool GlobalKnown = Number(Pricing, TEXT("credits_per_usd"), GlobalConversion) && GlobalConversion > 0;
    if (Pricing.IsValid() && Pricing->TryGetStringField(TEXT("unit"), Unit) && Unit == TEXT("usd_per_1m_tokens") && Pricing->TryGetObjectField(TEXT("models"), Prices))
    {
        for (const auto& Pair : (*Prices)->Values)
        {
            if (Pair.Value->Type != EJson::Object) { continue; }
            const auto Price = Pair.Value->AsObject(); FKHUPrice P; double Conversion = 0;
            if (!Number(Price, TEXT("in"), P.Input) || !Number(Price, TEXT("out"), P.Output)) { continue; }
            P.CachedInput = P.Input; Number(Price, TEXT("cached_in"), P.CachedInput);
            Number(Price, TEXT("cache_write"), P.CacheWrite); Number(Price, TEXT("cache_write_1h"), P.CacheWriteHour);
            if (Number(Price, TEXT("credits_per_usd"), Conversion) && Conversion > 0) { P.CreditsPerUsd = Conversion; }
            else if (GlobalKnown) { P.CreditsPerUsd = GlobalConversion; }
            C.Prices.Add(FString(*Pair.Key), P);
        }
    }
    Number(Budget, TEXT("per_task_soft"), C.PerTaskCredits); Number(Budget, TEXT("per_task_hard"), C.PerTaskHardCredits);
    Number(Budget, TEXT("semester_total"), C.SemesterCredits);
    Number(Context, TEXT("tokens_per_utf8_byte"), C.Context.TokensPerByte);
    Number(Context, TEXT("minimum_tokens_per_byte"), C.Context.MinRatio);
    Number(Context, TEXT("maximum_tokens_per_byte"), C.Context.MaxRatio);
    Number(Context, TEXT("safety_multiplier"), C.Context.Safety); Number(Context, TEXT("ema_alpha"), C.Context.Alpha);
    auto ContextInteger = [&Context](const TCHAR* Name, int32 Minimum, int32 Maximum, int32& Out)
    {
        double Value = 0;
        if (!Number(Context, Name, Value) || Value < Minimum || Value > Maximum || FMath::FloorToDouble(Value) != Value) { return false; }
        Out = static_cast<int32>(Value); return true;
    };
    const bool ContextValid = ContextInteger(TEXT("request_max_bytes"), 1024, 4194304, C.Context.MaxBytes)
        && ContextInteger(TEXT("max_messages"), 4, 128, C.Context.MaxMessages)
        && ContextInteger(TEXT("recent_turns"), 1, 32, C.Context.RecentTurns)
        && ContextInteger(TEXT("token_overhead"), 0, 1024, C.Context.TokenOverhead)
        && C.Context.MinRatio > 0 && C.Context.MinRatio <= C.Context.TokensPerByte
        && C.Context.TokensPerByte <= C.Context.MaxRatio && C.Context.MaxRatio <= 2
        && C.Context.Safety >= 1 && C.Context.Safety <= 4 && C.Context.Alpha > 0 && C.Context.Alpha <= 1;
    if (Tools.IsValid())
    {
        for (const TCHAR* Group : {TEXT("read"), TEXT("show"), TEXT("act"), TEXT("epic_t2_extra")}) { C.ToolGroups.Add(Group, Strings(Tools, Group)); }
        C.Never = Strings(Tools, TEXT("never"));
    }
    C.Never.Append({TEXT("*python*"), TEXT("*execute*"), TEXT("*console*"), TEXT("*command*"), TEXT("*shell*"), TEXT("*delete*")});
    double MaxOutput = 0, HardOutput = 0, ResultTokens = 0, Timeout = 0, Limit = 0, Spent = 0;
    if (Gateway.IsValid())
    {
        Gateway->TryGetStringField(TEXT("base_url"), C.BaseUrl);
        Number(Gateway, TEXT("max_tokens"), MaxOutput); Number(Gateway, TEXT("output_hard_limit"), HardOutput);
        Number(Gateway, TEXT("tool_result_max_tokens"), ResultTokens); Number(Gateway, TEXT("timeout_seconds"), Timeout);
    }
    C.MaxOutputTokens = static_cast<int32>(FMath::Clamp(MaxOutput, 0.0, 16000.0));
    C.OutputHardLimit = static_cast<int32>(FMath::Clamp(HardOutput, 0.0, 32000.0));
    C.ResultMaxTokens = static_cast<int32>(FMath::Clamp(ResultTokens, 0.0, 16000.0));
    C.TimeoutSeconds = static_cast<float>(FMath::Clamp(Timeout, 0.0, 180.0));
    bool Authorized = false;
    if (Development.IsValid())
    {
        Development->TryGetBoolField(TEXT("paid_calls_authorized"), Authorized);
        bool Unlimited = false; Development->TryGetBoolField(TEXT("unlimited_authorized"), Unlimited);
        C.bDevelopmentUnlimited = Authorized && Unlimited;
        C.DevelopmentModels = Strings(Development, TEXT("allowed_models"));
    }
    if (Authorized && Number(Development, TEXT("cumulative_credit_limit"), Limit) && Number(Development, TEXT("spent_verified_credits"), Spent))
    { C.DevelopmentRemaining = FMath::Max(0.0, Limit - Spent); }
    FFileHelper::LoadFileToString(C.SystemPrompt, *(Directory / TEXT("Prompts/system.txt")));
    // L0 end-of-request reminder lives beside the product prompt so it is versioned and hashed with it, not compiled into code.
    if (FFileHelper::LoadFileToString(C.L0Contract, *(Directory / TEXT("Prompts/l0_contract.txt")))) { C.L0Contract.TrimStartAndEndInline(); }
    C.FixtureDirectory = Directory / TEXT("Scenarios");
    bool MemoryValid = true;
    if (Context.IsValid() && Context->TryGetBoolField(TEXT("project_memory_enabled"), C.Context.bProjectMemory) && C.Context.bProjectMemory)
    {
        MemoryValid = ContextInteger(TEXT("session_max_turns"), 2, 32, C.Context.SessionTurns)
            && ContextInteger(TEXT("session_rotate_estimated_tokens"), 1024, 20000, C.Context.RotateTokens)
            && ContextInteger(TEXT("carryover_max_chars"), 256, 6000, C.Context.MemoryChars)
            && ContextInteger(TEXT("project_memory_max_entries"), 1, 20, C.Context.ProjectEntries)
            && ContextInteger(TEXT("project_memory_archive_max_bytes"), 65536, 8388608, C.Context.ArchiveMaxBytes)
            && ContextInteger(TEXT("project_memory_archive_max_turns"), 16, 8192, C.Context.ArchiveMaxTurns);
    }
    C.bValid = C.Warnings.IsEmpty() && C.Tiers.Contains(TEXT("T1")) && C.Tiers.Contains(TEXT("T2"))
        && ContextValid && MemoryValid && C.PerTaskCredits > 0 && C.PerTaskHardCredits >= C.PerTaskCredits && C.PerTaskHardCredits <= C.SemesterCredits
        && C.SemesterCredits > 0 && C.MaxOutputTokens > 0 && C.OutputHardLimit >= C.MaxOutputTokens
        && C.ResultMaxTokens > 0 && C.TimeoutSeconds > 0 && !C.SystemPrompt.IsEmpty() && !C.L0Contract.IsEmpty()
        && C.BaseUrl == TEXT("https://factchat-cloud.mindlogic.ai/v1/gateway");
    if (!C.bValid) { C.Warnings.Add(TEXT("Configuration incomplete or unsafe; default is Mock only")); }
    // Optional for legacy fixtures. A malformed present runtime file fails closed.
    const FString RuntimeFile = Directory / TEXT("ClaudeRuntime.json");
    if (FPaths::FileExists(RuntimeFile))
    {
        const auto Runtime = ReadConfig(Directory, TEXT("ClaudeRuntime.json"), C.Warnings);
        double TimeoutValue = 0, ContextValue = 0;
        if (!Runtime || !Runtime->TryGetBoolField(TEXT("panel_enabled"), C.bClaudePanel)
            || !Runtime->TryGetStringField(TEXT("tier"), C.ClaudeTier)
            || !Number(Runtime, TEXT("timeout_seconds"), TimeoutValue) || TimeoutValue < 30 || TimeoutValue > 600
            || !Number(Runtime, TEXT("context_tokens"), ContextValue) || ContextValue < 4096 || ContextValue > 100000
            || !C.Tiers.Contains(C.ClaudeTier)) { C.bValid = false; }
        else { C.ClaudeTimeout = static_cast<int32>(TimeoutValue); C.ClaudeContextTokens = static_cast<int32>(ContextValue); }
        if (Runtime) { Runtime->TryGetStringField(TEXT("executable"), C.ClaudeExecutable); }
        if (Runtime && Runtime->HasField(TEXT("prompt_cache_ttl")))
        { if (!Runtime->TryGetStringField(TEXT("prompt_cache_ttl"), C.ClaudePromptCacheTtl) || (C.ClaudePromptCacheTtl != TEXT("5m") && C.ClaudePromptCacheTtl != TEXT("1h"))) { C.bValid = false; C.Warnings.Add(TEXT("prompt_cache_ttl must be 5m or 1h")); } }
    }
    return C;
}

bool FKHUConfig::IsModelEnabled(const FString& Model, bool bDevelopment) const
{
    if (Model.IsEmpty()) { return false; }
    if (bDevelopment) { for (const auto& Allowed : DevelopmentModels) { if (Allowed.Equals(Model, ESearchCase::CaseSensitive)) { return true; } } }
    for (const auto& Pattern : DisabledModels) { if (Model.ToLower().MatchesWildcard(Pattern.ToLower())) { return false; } }
    return true;
}
TOptional<double> FKHUConfig::Cost(const FString& Model, int64 Input, int64 Cached, int64 Output) const
{
    const auto* P = Prices.Find(Model);
    if (!P || !P->CreditsPerUsd.IsSet() || Input < 0 || Cached < 0 || Cached > Input || Output < 0) { return {}; }
    const double Result = ((Input - Cached) * P->Input + Cached * P->CachedInput + Output * P->Output) / 1000000.0 * P->CreditsPerUsd.GetValue();
    return FMath::IsFinite(Result) && Result >= 0 ? TOptional<double>(Result) : TOptional<double>();
}

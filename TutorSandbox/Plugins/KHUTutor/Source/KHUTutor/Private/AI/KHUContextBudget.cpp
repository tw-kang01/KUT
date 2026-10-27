#include "AI/KHUContextBudget.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
    constexpr auto OmissionPrefix = TEXT("KHU_CONTEXT_OMITTED:");
    FString Role(const TSharedPtr<FJsonValue>& V)
    {
        FString R;
        if (V.IsValid() && V->Type == EJson::Object) { V->AsObject()->TryGetStringField(TEXT("role"), R); }
        return R;
    }
    FString Body(const FKHUChatRequest& R) { return R.bResponses ? KHUResponsesProtocol::Body(R) : KHUChatProtocol::Body(R); }
    FString Segment(const FString& Name)
    {
        if (Name.EndsWith(TEXT("GetLogDigest")) || Name.EndsWith(TEXT("GetCppBuildErrors"))) { return TEXT("ctx_log"); }
        if (Name.EndsWith(TEXT("GetBlueprintGraphDigest")) || Name.EndsWith(TEXT("GetBlueprintCompileErrors"))) { return TEXT("ctx_graph"); }
        if (Name.EndsWith(TEXT("GetTutorContext")) || Name.EndsWith(TEXT("Ping"))) { return TEXT("ctx_static"); }
        if (Name.EndsWith(TEXT("ReadProjectSource"))) { return TEXT("ctx_other"); }
        return TEXT("tool_result");
    }
    TSharedRef<FJsonObject> Segments(const FKHUChatRequest& R, const FString& Serialized, const FKHUTokenEstimator& Estimator)
    {
        TMap<FString, int32> Bytes;
        for (const TCHAR* K : {TEXT("system"), TEXT("tools"), TEXT("ctx_static"), TEXT("history"), TEXT("ctx_log"), TEXT("ctx_graph"), TEXT("ctx_other"), TEXT("tool_result"), TEXT("user")}) { Bytes.Add(K, 0); }
        TMap<FString, FString> Names;
        for (const auto& M : R.Messages)
        {
            const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
            if (M->AsObject()->TryGetArrayField(TEXT("tool_calls"), Calls))
            {
                for (const auto& C : *Calls) { Names.Add(C->AsObject()->GetStringField(TEXT("id")), C->AsObject()->GetObjectField(TEXT("function"))->GetStringField(TEXT("name"))); }
            }
        }
        TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Serialized, O);
        const auto& Input = O->GetArrayField(R.bResponses ? TEXT("input") : TEXT("messages"));
        int32 LastUser = INDEX_NONE;
        for (int32 I = 0; I < Input.Num(); ++I) { if (Role(Input[I]) == TEXT("user")) { LastUser = I; } }
        for (int32 I = 0; I < Input.Num(); ++I)
        {
            const auto M = Input[I]->AsObject(); FString Kind = TEXT("history"), Type, Id;
            M->TryGetStringField(TEXT("type"), Type);
            if (Role(Input[I]) == TEXT("system")) { Kind = TEXT("system"); }
            else if (I == LastUser) { Kind = TEXT("user"); }
            else if (Role(Input[I]) == TEXT("tool") || Type == TEXT("function_call_output"))
            {
                M->TryGetStringField(R.bResponses ? TEXT("call_id") : TEXT("tool_call_id"), Id);
                Kind = Segment(Names.FindRef(Id));
            }
            Bytes.FindOrAdd(Kind) += FKHUContextBudgeter::Utf8Bytes(KHUMcpProtocol::WriteObject(M.ToSharedRef()));
        }
        FKHUChatRequest OnlyTools = R; OnlyTools.Messages.Reset();
        FKHUChatRequest Empty = OnlyTools; Empty.Tools.Reset(); Empty.SerializedTools.Reset();
        Bytes[TEXT("tools")] = FMath::Max(0, FKHUContextBudgeter::Utf8Bytes(Body(OnlyTools)) - FKHUContextBudgeter::Utf8Bytes(Body(Empty)));
        auto Result = MakeShared<FJsonObject>();
        for (const auto& Pair : Bytes)
        {
            auto S = MakeShared<FJsonObject>(); S->SetNumberField(TEXT("utf8_bytes"), Pair.Value);
            S->SetNumberField(TEXT("estimated_tokens"), Estimator.Estimate(R.Model, Pair.Value, false));
            Result->SetObjectField(Pair.Key, S);
        }
        return Result;
    }
}

FKHUTokenEstimator::FKHUTokenEstimator(FKHUContextPolicy InPolicy, FString InPath) : Policy(InPolicy), Path(MoveTemp(InPath)) { Load(); }
void FKHUTokenEstimator::Load()
{
    if (Path.IsEmpty()) { return; }
    FString Text; TSharedPtr<FJsonObject> O; const TSharedPtr<FJsonObject>* Models = nullptr;
    if (!FFileHelper::LoadFileToString(Text, *Path) || Text.Len() > 65536 || !KHUMcpProtocol::ReadObject(Text, O)
        || !O->TryGetObjectField(TEXT("models"), Models) || (*Models)->Values.Num() > 64) { return; }
    for (const auto& Pair : (*Models)->Values)
    {
        double RatioValue = 0, Count = 0;
        if (Pair.Key.Len() > 128 || Pair.Value->Type != EJson::Object) { continue; }
        const auto V = Pair.Value->AsObject();
        if (V->TryGetNumberField(TEXT("ratio"), RatioValue) && V->TryGetNumberField(TEXT("samples"), Count)
            && FMath::IsFinite(RatioValue) && RatioValue >= Policy.MinRatio && RatioValue <= Policy.MaxRatio
            && FMath::IsFinite(Count) && Count >= 1 && Count <= 1000000 && FMath::FloorToDouble(Count) == Count)
        { Calibration.Add(FString(*Pair.Key), TPair<double, int32>(RatioValue, static_cast<int32>(Count))); }
    }
}
double FKHUTokenEstimator::Ratio(const FString& Model) const
{ const auto* V = Calibration.Find(Model); return V ? V->Key : Policy.TokensPerByte; }
int32 FKHUTokenEstimator::Samples(const FString& Model) const
{ const auto* V = Calibration.Find(Model); return V ? V->Value : 0; }
int32 FKHUTokenEstimator::Estimate(const FString& Model, int32 Bytes, bool bOverhead) const
{ return FMath::CeilToInt32(FMath::Max(0, Bytes) * Ratio(Model) * Policy.Safety) + (bOverhead ? Policy.TokenOverhead : 0); }
bool FKHUTokenEstimator::Observe(const FString& Model, int32 Bytes, int64 Tokens)
{
    if (Model.IsEmpty() || Model.Len() > 128 || Bytes <= 0 || Bytes > Policy.MaxBytes || Tokens <= 0 || Tokens > 10000000) { return false; }
    // Merge prior model entries before writing so separate editor panels do not erase one another.
    Load();
    if (!Calibration.Contains(Model) && Calibration.Num() >= 64) { return false; }
    const double Measured = FMath::Clamp(static_cast<double>(Tokens) / Bytes, Policy.MinRatio, Policy.MaxRatio);
    const double Updated = FMath::Clamp(Ratio(Model) * (1 - Policy.Alpha) + Measured * Policy.Alpha, Policy.MinRatio, Policy.MaxRatio);
    Calibration.Add(Model, TPair<double, int32>(Updated, FMath::Min(1000000, Samples(Model) + 1)));
    if (!Path.IsEmpty())
    {
        auto O = MakeShared<FJsonObject>(), Models = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("version"), 1);
        for (const auto& Pair : Calibration)
        {
            auto V = MakeShared<FJsonObject>(); V->SetNumberField(TEXT("ratio"), Pair.Value.Key); V->SetNumberField(TEXT("samples"), Pair.Value.Value);
            Models->SetObjectField(Pair.Key, V);
        }
        O->SetObjectField(TEXT("models"), Models); IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        // A persistence failure never changes a usage receipt or disables the in-memory bound.
        return FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(O), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    return true;
}

int32 FKHUContextBudgeter::Utf8Bytes(const FString& Text) { const FTCHARToUTF8 Bytes(*Text); return Bytes.Length(); }
bool FKHUContextBudgeter::Validate(const TArray<TSharedPtr<FJsonValue>>& Messages)
{
    TSet<FString> Pending, Seen;
    for (const auto& V : Messages)
    {
        if (!V.IsValid() || V->Type != EJson::Object) { return false; }
        const auto M = V->AsObject(); const FString R = Role(V);
        if (R == TEXT("tool"))
        {
            FString Id;
            if (!M->TryGetStringField(TEXT("tool_call_id"), Id) || !Pending.Remove(Id)) { return false; }
            continue;
        }
        if (!Pending.IsEmpty() || (R != TEXT("system") && R != TEXT("user") && R != TEXT("assistant"))) { return false; }
        const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
        if (M->TryGetArrayField(TEXT("tool_calls"), Calls))
        {
            if (R != TEXT("assistant") || Calls->Num() > 16) { return false; }
            for (const auto& C : *Calls)
            {
                FString Id; const TSharedPtr<FJsonObject>* F = nullptr;
                if (!C.IsValid() || C->Type != EJson::Object || !C->AsObject()->TryGetStringField(TEXT("id"), Id)
                    || Id.IsEmpty() || Seen.Contains(Id) || !C->AsObject()->TryGetObjectField(TEXT("function"), F)) { return false; }
                FString Name, Args;
                if (!(*F)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || !(*F)->TryGetStringField(TEXT("arguments"), Args)) { return false; }
                Seen.Add(Id); Pending.Add(Id);
            }
        }
        const TArray<TSharedPtr<FJsonValue>>* Raw = nullptr;
        if (M->TryGetArrayField(TEXT("_responses_output"), Raw))
        {
            if (R != TEXT("assistant")) { return false; }
            TSet<FString> RawCalls;
            for (const auto& Item : *Raw)
            {
                FString Type, Id;
                if (!Item.IsValid() || Item->Type != EJson::Object || !Item->AsObject()->TryGetStringField(TEXT("type"), Type)) { return false; }
                if (Type == TEXT("function_call"))
                {
                    if (!Item->AsObject()->TryGetStringField(TEXT("call_id"), Id) || !Pending.Contains(Id) || RawCalls.Contains(Id)) { return false; }
                    RawCalls.Add(Id);
                }
                else if (Type != TEXT("reasoning") && Type != TEXT("message")) { return false; }
            }
            if (RawCalls.Num() != Pending.Num()) { return false; }
        }
    }
    return Pending.IsEmpty();
}
FKHUContextResult FKHUContextBudgeter::Build(FKHUChatRequest& Request, const FKHUContextPolicy& Policy,
    const FKHUTokenEstimator& Estimator, int32 TokenLimit)
{
    FKHUContextResult Result;
    if (TokenLimit <= 0 || Policy.MaxBytes <= 0 || !Validate(Request.Messages))
    { Result.Error = TEXT("문맥 설정 또는 도구 호출·결과 쌍이 올바르지 않아 요청을 중단했습니다."); return Result; }
    struct FTurn { TArray<TSharedPtr<FJsonValue>> Items; bool Keep = true, Pinned = false, Evidence = false; };
    TArray<FTurn> Turns; TArray<TSharedPtr<FJsonValue>> System; TSharedPtr<FJsonValue> Original;
    for (const auto& V : Request.Messages)
    {
        if (Role(V) == TEXT("system"))
        {
            FString Content; V->AsObject()->TryGetStringField(TEXT("content"), Content);
            if (!Content.StartsWith(OmissionPrefix)) { System.Add(V); }
            continue;
        }
        if (Role(V) == TEXT("user")) { Turns.AddDefaulted(); if (!Original.IsValid()) { Original = V; } }
        if (Turns.IsEmpty()) { Result.Error = TEXT("질문이 없는 문맥은 전송하지 않습니다."); return Result; }
        Turns.Last().Items.Add(V); Turns.Last().Evidence |= Role(V) == TEXT("tool");
    }
    if (Turns.IsEmpty()) { Result.Error = TEXT("질문이 없는 문맥은 전송하지 않습니다."); return Result; }
    Turns.Last().Pinned = true;
    // Keep the newest evidence-bearing turn even when the current follow-up is only 'more hints'.
    for (int32 I = Turns.Num() - 1; I >= 0; --I) { if (Turns[I].Evidence) { Turns[I].Pinned = true; break; } }
    if (Turns[0].Items.Num() == 1) { Turns[0].Pinned = true; }
    int32 Kept = Turns.Num(), Removed = 0;
    auto RemoveOldest = [&Turns, &Kept, &Removed]()
    {
        for (auto& T : Turns) { if (T.Keep && !T.Pinned) { T.Keep = false; --Kept; ++Removed; return true; } }
        return false;
    };
    while (Kept > Policy.RecentTurns && RemoveOldest()) {}
    for (;;)
    {
        FKHUChatRequest Candidate = Request; Candidate.Messages = System;
        Candidate.OmittedTurns = Request.OmittedTurns + Removed;
        if (Candidate.OmittedTurns > 0)
        {
            auto Notice = MakeShared<FJsonObject>(); Notice->SetStringField(TEXT("role"), TEXT("system"));
            Notice->SetStringField(TEXT("content"), TEXT("KHU_CONTEXT_OMITTED: Earlier completed turns were omitted to fit the input budget. Do not assume missing evidence; use read tools if needed. Original and current questions and the latest tool evidence are retained."));
            Candidate.Messages.Add(MakeShared<FJsonValueObject>(Notice));
        }
        if (!Turns[0].Keep) { Candidate.Messages.Add(Original); }
        for (const auto& T : Turns) { if (T.Keep) { Candidate.Messages.Append(T.Items); } }
        const FString Serialized = Body(Candidate); const int32 Bytes = Utf8Bytes(Serialized), Tokens = Estimator.Estimate(Request.Model, Bytes);
        if (Bytes <= Policy.MaxBytes && Tokens <= TokenLimit && Candidate.Messages.Num() <= Policy.MaxMessages)
        {
            Candidate.InputPayloadBytes = Bytes; Candidate.EstimatedInputTokens = Tokens;
            Result.bSuccess = true; Result.InputBytes = Bytes; Result.EstimatedTokens = Tokens; Result.OmittedTurns = Candidate.OmittedTurns;
            Result.Segments = Segments(Candidate, Serialized, Estimator); Request = MoveTemp(Candidate); return Result;
        }
        if (!RemoveOldest())
        { Result.Error = TEXT("현재 질문과 필요한 근거만으로도 문맥 한도를 넘습니다. 질문을 나누거나 더 작은 범위의 새 작업으로 시작하세요."); return Result; }
    }
}

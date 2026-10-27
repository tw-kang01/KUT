#include "AI/KHUChatClient.h"
#include "AI/KHUSecretStore.h"
#include "AI/KHUContextBudget.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"

FString KHUChatProtocol::Body(const FKHUChatRequest& R)
{
    auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("model"), R.Model); O->SetArrayField(TEXT("messages"), R.Messages);
    O->SetNumberField(TEXT("max_tokens"), R.MaxTokens); O->SetBoolField(TEXT("stream"), false);
    if (!R.Tools.IsEmpty()) { if (R.SerializedTools.IsEmpty()) { O->SetArrayField(TEXT("tools"), R.Tools); } O->SetStringField(TEXT("tool_choice"), TEXT("auto")); }
    if (!R.ReasoningEffort.IsEmpty()) { O->SetStringField(TEXT("reasoning_effort"), R.ReasoningEffort); }
    else if (R.Temperature.IsSet()) { O->SetNumberField(TEXT("temperature"), R.Temperature.GetValue()); }
    FString Body = KHUMcpProtocol::WriteObject(O);
    if (!R.Tools.IsEmpty() && !R.SerializedTools.IsEmpty()) { Body = Body.LeftChop(1) + TEXT(",\"tools\":") + R.SerializedTools + TEXT("}"); }
    return Body;
}
FKHUChatResponse KHUChatProtocol::Parse(const FString& Body)
{
    FKHUChatResponse R; TSharedPtr<FJsonObject> O; const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    R.Error = TEXT("모델 응답 형식이 올바르지 않습니다.");
    if (!KHUMcpProtocol::ReadObject(Body, O) || !O->TryGetArrayField(TEXT("choices"), Choices) || Choices->IsEmpty() || (*Choices)[0]->Type != EJson::Object) { return R; }
    const auto Choice = (*Choices)[0]->AsObject(); const TSharedPtr<FJsonObject>* Message = nullptr;
    if (!Choice->TryGetObjectField(TEXT("message"), Message)) { return R; }
    FString Role; if (!(*Message)->TryGetStringField(TEXT("role"), Role) || Role != TEXT("assistant")) { return R; }
    (*Message)->TryGetStringField(TEXT("content"), R.Content); Choice->TryGetStringField(TEXT("finish_reason"), R.FinishReason);
    const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr; TSet<FString> Ids;
    if ((*Message)->TryGetArrayField(TEXT("tool_calls"), Calls))
    {
        if (Calls->Num() > 16) { return R; }
        for (const auto& V : *Calls)
        {
            if (V->Type != EJson::Object) { return R; } const auto Call = V->AsObject(); FKHUToolCall T; FString Type; const TSharedPtr<FJsonObject>* F = nullptr;
            if (!Call->TryGetStringField(TEXT("id"), T.Id) || T.Id.IsEmpty() || Ids.Contains(T.Id)
                || !Call->TryGetStringField(TEXT("type"), Type) || Type != TEXT("function") || !Call->TryGetObjectField(TEXT("function"), F)
                || !(*F)->TryGetStringField(TEXT("name"), T.Name) || T.Name.IsEmpty() || !(*F)->TryGetStringField(TEXT("arguments"), T.Arguments) || T.Arguments.Len() > 65536) { return R; }
            Ids.Add(T.Id); R.Calls.Add(MoveTemp(T));
        }
    }
    const TSharedPtr<FJsonObject>* Usage = nullptr;
    if (O->TryGetObjectField(TEXT("usage"), Usage))
    {
        double In = 0, Out = 0, Cached = 0; const TSharedPtr<FJsonObject>* Details = nullptr;
        if ((*Usage)->TryGetObjectField(TEXT("prompt_tokens_details"), Details)) { (*Details)->TryGetNumberField(TEXT("cached_tokens"), Cached); }
        if ((*Usage)->TryGetNumberField(TEXT("prompt_tokens"), In) && (*Usage)->TryGetNumberField(TEXT("completion_tokens"), Out)
            && FMath::IsFinite(In) && FMath::IsFinite(Out) && FMath::IsFinite(Cached) && In >= 0 && Out >= 0 && Cached >= 0 && Cached <= In
            && In <= 10000000 && Out <= 10000000 && FMath::FloorToDouble(In) == In && FMath::FloorToDouble(Out) == Out && FMath::FloorToDouble(Cached) == Cached)
        { R.bUsageKnown = true; R.InputTokens = static_cast<int64>(In); R.OutputTokens = static_cast<int64>(Out); R.CachedTokens = static_cast<int64>(Cached); }
    }
    double Credits = 0; if (O->TryGetNumberField(TEXT("credits"), Credits) && FMath::IsFinite(Credits) && Credits >= 0) { R.Credits = Credits; }
    R.Assistant = MakeShared<FJsonObject>(); R.Assistant->SetStringField(TEXT("role"), TEXT("assistant"));
    if (R.Content.IsEmpty()) { R.Assistant->SetField(TEXT("content"), MakeShared<FJsonValueNull>()); } else { R.Assistant->SetStringField(TEXT("content"), R.Content); }
    if (Calls && !Calls->IsEmpty()) { R.Assistant->SetArrayField(TEXT("tool_calls"), *Calls); }
    R.bSuccess = true; R.Error.Reset(); return R;
}
FString KHUChatProtocol::Error(int32 Status)
{
    switch (Status)
    {
    case 401: return TEXT("API 키와 ChatKHU Base URL을 확인하세요.");
    case 402: return TEXT("ChatKHU 크레딧이 부족합니다.");
    case 403: return TEXT("이 조직에서 API Gateway 사용이 허용되는지 확인하세요.");
    case 404: return TEXT("사용할 수 없는 모델입니다. 다음 모델을 확인합니다.");
    case 413: return TEXT("요청이 너무 큽니다. 문맥을 줄인 재요청도 실패했습니다.");
    case 429: return TEXT("요청이 몰려 있습니다. 잠시 후 다시 시도하세요.");
    default: return Status == 0 || Status >= 500 ? TEXT("ChatKHU 연결이 지연되거나 서버 오류가 발생했습니다.") : TEXT("ChatKHU가 요청을 거절했습니다.");
    }
}
TSharedRef<FKHUMockChatClient> FKHUMockChatClient::Load(const FString& Path)
{
    FString Text; TSharedPtr<FJsonObject> O; TArray<FString> Responses; const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (FFileHelper::LoadFileToString(Text, *Path) && KHUMcpProtocol::ReadObject(Text, O) && O->TryGetArrayField(TEXT("responses"), Array))
    { for (const auto& V : *Array) { if (V->Type == EJson::Object) { Responses.Add(KHUMcpProtocol::WriteObject(V->AsObject().ToSharedRef())); } } }
    return MakeShared<FKHUMockChatClient>(MoveTemp(Responses));
}
void FKHUMockChatClient::Send(FKHUChatRequest Request, FCallback Callback)
{
    const FString Body = Responses.IsValidIndex(Index) ? Responses[Index++] : TEXT("{}");
    const uint64 G = Generation; const TWeakPtr<FKHUMockChatClient> Weak = AsShared();
    AsyncTask(ENamedThreads::GameThread, [Weak, G, Body, Callback = MoveTemp(Callback)]() mutable
    { if (auto Self = Weak.Pin(); Self && Self->Generation == G) { auto R = KHUChatProtocol::Parse(Body); R.bMock = true; Callback(MoveTemp(R)); } });
}
FKHUChatClient::FKHUChatClient(FKHUConfig InConfig, FString InKey, TSharedRef<IKHUMcpTransport> InTransport)
    : Config(MoveTemp(InConfig)), Key(MoveTemp(InKey)), Transport(InTransport) {}
FKHUChatClient::~FKHUChatClient() { Cancel(); Key.Reset(); }
void FKHUChatClient::Cancel()
{
    ++Generation; Completion = {}; if (CancelHttp) { auto C = MoveTemp(CancelHttp); C(); }
    FTSTicker::RemoveTicker(RetryHandle); RetryHandle.Reset();
}
void FKHUChatClient::Send(FKHUChatRequest Request, FCallback Callback)
{
    if (Completion) { FKHUChatResponse R; R.Error = TEXT("이전 모델 요청이 진행 중입니다."); Callback(MoveTemp(R)); return; }
    Current = MoveTemp(Request); Completion = MoveTemp(Callback); const uint64 G = ++Generation;
    Attempts = RateRetries = TransientRetries = LastPostedBytes = 0; bHalved = bExpanded = false;
    if (!Config.bValid || !FKHUSecretStore::IsValidKey(Key) || (Current.bUnlimitedDevelopment && !Config.bDevelopmentUnlimited)
        || !Config.IsModelEnabled(Current.Model, Current.bUnlimitedDevelopment) || Current.MaxTokens <= 0 || Current.OutputHardLimit < Current.MaxTokens)
    { FKHUChatResponse R; R.Error = TEXT("설정 또는 API 키가 올바르지 않아 실제 호출을 중단했습니다."); Finish(MoveTemp(R)); return; }
    if (!bDiscovered) { Discover(G); } else { Post(G); }
}
void FKHUChatClient::Discover(uint64 G)
{
    FKHUMcpHttpRequest R; R.Url = Config.BaseUrl + TEXT("/models/"); R.Verb = TEXT("GET"); R.TimeoutSeconds = Config.TimeoutSeconds;
    R.Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + Key); R.Headers.Add(TEXT("User-Agent"), TEXT("KHU-UE-Tutor-D7/0.1"));
    const TWeakPtr<FKHUChatClient> Weak = AsShared();
    const uint64 Serial = ++NetworkSerial;
    auto CancelRequest = Transport->Send(R, [Weak, G](FKHUMcpHttpResponse Response)
    {
        const auto Self = Weak.Pin(); if (!Self || Self->Generation != G || !Self->Completion) { return; }
        TSharedPtr<FJsonObject> O; const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
        if (Response.Status != 200 || !KHUMcpProtocol::ReadObject(Response.Body, O) || !O->TryGetArrayField(TEXT("data"), Data))
        { FKHUChatResponse Result; Result.HttpStatus = Response.Status; Result.Error = KHUChatProtocol::Error(Response.Status); Self->Finish(MoveTemp(Result)); return; }
        for (const auto& V : *Data)
        {
            if (V->Type != EJson::Object) { continue; } const auto M = V->AsObject(); FString Id, Type; const TArray<TSharedPtr<FJsonValue>>* Endpoints = nullptr;
            if (M->TryGetStringField(TEXT("id"), Id) && M->TryGetStringField(TEXT("type"), Type) && Type == TEXT("llm") && M->TryGetArrayField(TEXT("endpoints"), Endpoints))
            {
                for (const auto& E : *Endpoints)
                {
                    if (E->AsString() == TEXT("/v1/gateway/chat/completions/")) { Self->Models.Add(Id); }
                    if (E->AsString() == TEXT("/v1/gateway/responses/")) { Self->ResponseModels.Add(Id); }
                }
            }
        }
        Self->bDiscovered = true; Self->Post(G);
    });
    if (Generation == G && NetworkSerial == Serial && Completion) { CancelHttp = MoveTemp(CancelRequest); } else if (CancelRequest) { CancelRequest(); }
}
void FKHUChatClient::Post(uint64 G)
{
    if (Generation != G || !Completion) { return; }
    if (!(Current.bResponses ? ResponseModels : Models).Contains(Current.Model)) { FKHUChatResponse R; R.bTryNextModel = true; R.Error = KHUChatProtocol::Error(404); Finish(MoveTemp(R)); return; }
    if (!Current.bUnlimitedDevelopment && (Current.AttemptCreditReserve <= 0 || Current.AttemptCreditReserve * (Attempts + 1) > Current.RemainingCredits))
    { FKHUChatResponse R; R.bBudget = true; R.Error = TEXT("재요청을 포함한 예상 비용이 남은 작업 예산을 넘습니다."); Finish(MoveTemp(R)); return; }
    FKHUMcpHttpRequest R; R.Url = Config.BaseUrl + (Current.bResponses ? TEXT("/responses/") : TEXT("/chat/completions/"));
    R.Body = FKHUSecretStore::Redact(Current.bResponses ? KHUResponsesProtocol::Body(Current) : KHUChatProtocol::Body(Current), Key); R.TimeoutSeconds = Config.TimeoutSeconds;
    LastPostedBytes = FKHUContextBudgeter::Utf8Bytes(R.Body);
    R.Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + Key); R.Headers.Add(TEXT("User-Agent"), TEXT("KHU-UE-Tutor-D7/0.1")); R.Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
    ++Attempts; const TWeakPtr<FKHUChatClient> Weak = AsShared();
    const uint64 Serial = ++NetworkSerial;
    auto CancelRequest = Transport->Send(R, [Weak, G](FKHUMcpHttpResponse Response)
    { if (auto Self = Weak.Pin(); Self && Self->Generation == G && Self->Completion) { Self->Received(G, MoveTemp(Response)); } });
    if (Generation == G && NetworkSerial == Serial && Completion) { CancelHttp = MoveTemp(CancelRequest); } else if (CancelRequest) { CancelRequest(); }
}
void FKHUChatClient::Retry(uint64 G, float Seconds)
{
    const TWeakPtr<FKHUChatClient> Weak = AsShared();
    RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, G](float)
    { if (auto Self = Weak.Pin(); Self && Self->Generation == G) { Self->RetryHandle.Reset(); Self->Post(G); } return false; }), Seconds);
}
void FKHUChatClient::Received(uint64 G, FKHUMcpHttpResponse Response)
{
    if (Response.Status == 429 && RateRetries < 3)
    {
        float Delay = static_cast<float>(1 << (++RateRetries)); const FString Header = Response.Header(TEXT("Retry-After"));
        double Seconds = 0; if (!Header.IsEmpty() && LexTryParseString(Seconds, *Header) && FMath::IsFinite(Seconds) && Seconds >= 0) { Delay = static_cast<float>(FMath::Clamp(Seconds, 0.0, 300.0)); }
        Retry(G, Delay); return;
    }
    if ((Response.Status == 0 || Response.Status >= 500) && TransientRetries++ == 0) { Retry(G, 1); return; }
    if (Response.Status == 413 && !bHalved)
    {
        bHalved = true;
        const auto Estimator = FKHUTokenEstimator(Config.Context);
        const int32 Before = FKHUContextBudgeter::Utf8Bytes(Current.bResponses ? KHUResponsesProtocol::Body(Current) : KHUChatProtocol::Body(Current));
        const int32 TokenLimit = FMath::Max(1, Estimator.Estimate(Current.Model, Before) / 2);
        auto Smaller = Current; const auto Context = FKHUContextBudgeter::Build(Smaller, Config.Context, Estimator, TokenLimit);
        if (Context.bSuccess && Context.InputBytes < Before) { Current = MoveTemp(Smaller); Retry(G, 0); return; }
        // A protected prompt or call/result pair must never be cut in half to force a retry.
    }
    FKHUChatResponse R;
    if (Response.Status == 200)
    {
        const FString Safe = FKHUSecretStore::Redact(Response.Body, Key);
        R = Current.bResponses ? KHUResponsesProtocol::Parse(Safe) : KHUChatProtocol::Parse(Safe);
        if (R.bSuccess && R.Content.IsEmpty() && R.Calls.IsEmpty() && !bExpanded && Current.MaxTokens < Current.OutputHardLimit)
        { bExpanded = true; Current.MaxTokens = FMath::Min(Current.OutputHardLimit, Current.MaxTokens * 2); Retry(G, 0); return; }
        if (R.bSuccess && R.Content.IsEmpty() && R.Calls.IsEmpty()) { R.bSuccess = false; R.Error = TEXT("모델이 빈 응답을 반환했습니다."); }
    }
    else
    {
        R.Error = KHUChatProtocol::Error(Response.Status);
        R.ProviderError = FKHUSecretStore::Redact(Response.Body, Key).Left(2048);
        R.bTryNextModel = Response.Status == 404 || Response.Status >= 500 || Response.Status == 0;
    }
    R.HttpStatus = Response.Status; Finish(MoveTemp(R));
}
void FKHUChatClient::Finish(FKHUChatResponse R)
{
    R.InputPayloadBytes = LastPostedBytes;
    R.Attempts = Attempts; auto Callback = MoveTemp(Completion); if (Callback) { Callback(MoveTemp(R)); }
}

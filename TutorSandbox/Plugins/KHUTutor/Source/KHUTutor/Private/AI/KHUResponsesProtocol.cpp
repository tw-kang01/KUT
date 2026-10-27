#include "AI/KHUChatClient.h"
#include "Mcp/KHUMcpProtocol.h"

FString KHUResponsesProtocol::Body(const FKHUChatRequest& R)
{
    auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("model"), R.Model);
    O->SetBoolField(TEXT("store"), false); O->SetBoolField(TEXT("stream"), false);
    O->SetNumberField(TEXT("max_output_tokens"), R.MaxTokens);
    O->SetArrayField(TEXT("include"), {MakeShared<FJsonValueString>(TEXT("reasoning.encrypted_content"))});
    if (!R.ReasoningEffort.IsEmpty()) { auto Reasoning = MakeShared<FJsonObject>(); Reasoning->SetStringField(TEXT("effort"), R.ReasoningEffort); O->SetObjectField(TEXT("reasoning"), Reasoning); }
    else if (R.Temperature.IsSet()) { O->SetNumberField(TEXT("temperature"), R.Temperature.GetValue()); }
    TArray<TSharedPtr<FJsonValue>> Input;
    for (const auto& V : R.Messages)
    {
        const auto M = V->AsObject(); const TArray<TSharedPtr<FJsonValue>>* Raw = nullptr;
        // Preserve every output item, including encrypted reasoning, in original order.
        if (M->TryGetArrayField(TEXT("_responses_output"), Raw)) { Input.Append(*Raw); continue; }
        FString Role, Text; M->TryGetStringField(TEXT("role"), Role); M->TryGetStringField(TEXT("content"), Text);
        if (Role == TEXT("tool"))
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("type"), TEXT("function_call_output"));
            FString Id; M->TryGetStringField(TEXT("tool_call_id"), Id); Result->SetStringField(TEXT("call_id"), Id); Result->SetStringField(TEXT("output"), Text);
            Input.Add(MakeShared<FJsonValueObject>(Result)); continue;
        }
        if (!Text.IsEmpty())
        { auto Message = MakeShared<FJsonObject>(); Message->SetStringField(TEXT("role"), Role); Message->SetStringField(TEXT("content"), Text); Input.Add(MakeShared<FJsonValueObject>(Message)); }
        const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
        if (Role == TEXT("assistant") && M->TryGetArrayField(TEXT("tool_calls"), Calls))
        {
            for (const auto& C : *Calls)
            {
                const auto Call = C->AsObject(); const auto F = Call->GetObjectField(TEXT("function")); auto Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("type"), TEXT("function_call")); Item->SetStringField(TEXT("call_id"), Call->GetStringField(TEXT("id")));
                Item->SetStringField(TEXT("name"), F->GetStringField(TEXT("name"))); Item->SetStringField(TEXT("arguments"), F->GetStringField(TEXT("arguments")));
                Input.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }
    O->SetArrayField(TEXT("input"), Input);
    if (!R.Tools.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Tools;
        for (const auto& V : R.Tools)
        {
            const auto F = V->AsObject()->GetObjectField(TEXT("function")); auto Tool = MakeShared<FJsonObject>();
            Tool->SetStringField(TEXT("type"), TEXT("function")); Tool->SetStringField(TEXT("name"), F->GetStringField(TEXT("name")));
            Tool->SetStringField(TEXT("description"), F->GetStringField(TEXT("description"))); Tool->SetObjectField(TEXT("parameters"), F->GetObjectField(TEXT("parameters")));
            Tool->SetBoolField(TEXT("strict"), false); Tools.Add(MakeShared<FJsonValueObject>(Tool));
        }
        O->SetArrayField(TEXT("tools"), Tools); O->SetStringField(TEXT("tool_choice"), TEXT("auto")); O->SetBoolField(TEXT("parallel_tool_calls"), false);
    }
    return KHUMcpProtocol::WriteObject(O);
}
FKHUChatResponse KHUResponsesProtocol::Parse(const FString& Body)
{
    FKHUChatResponse Failed; Failed.Error = TEXT("Responses 응답 형식이 올바르지 않습니다.");
    TSharedPtr<FJsonObject> O; const TArray<TSharedPtr<FJsonValue>>* Output = nullptr; FString Status;
    if (!KHUMcpProtocol::ReadObject(Body, O) || !O->TryGetStringField(TEXT("status"), Status)) { return Failed; }
    if (Status != TEXT("completed")) { Failed.Error = TEXT("모델 응답이 완료되지 않았습니다. 출력 한도 또는 서버 상태를 확인하세요."); return Failed; }
    if (!O->TryGetArrayField(TEXT("output"), Output) || Output->Num() > 64) { return Failed; }
    FString Text; TArray<TSharedPtr<FJsonValue>> Calls;
    for (const auto& V : *Output)
    {
        if (!V.IsValid() || V->Type != EJson::Object) { return Failed; }
        const auto Item = V->AsObject(); FString Type;
        if (!Item->TryGetStringField(TEXT("type"), Type)) { return Failed; }
        if (Type == TEXT("reasoning")) { continue; }
        if (Type == TEXT("function_call"))
        {
            FString Id, Name, Args;
            if (!Item->TryGetStringField(TEXT("call_id"), Id) || !Item->TryGetStringField(TEXT("name"), Name) || !Item->TryGetStringField(TEXT("arguments"), Args)) { return Failed; }
            auto C = MakeShared<FJsonObject>(); auto F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("name"), Name); F->SetStringField(TEXT("arguments"), Args);
            C->SetStringField(TEXT("id"), Id); C->SetStringField(TEXT("type"), TEXT("function")); C->SetObjectField(TEXT("function"), F); Calls.Add(MakeShared<FJsonValueObject>(C));
        }
        else if (Type == TEXT("message"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
            if (!Item->TryGetArrayField(TEXT("content"), Content)) { return Failed; }
            for (const auto& Part : *Content)
            {
                if (Part->Type != EJson::Object) { return Failed; } FString Kind, T;
                if (!Part->AsObject()->TryGetStringField(TEXT("type"), Kind)) { return Failed; }
                if (Kind == TEXT("output_text") && Part->AsObject()->TryGetStringField(TEXT("text"), T)) { if (!Text.IsEmpty()) { Text += TEXT("\n"); } Text += T; }
                else if (Kind == TEXT("refusal") && Part->AsObject()->TryGetStringField(TEXT("refusal"), T)) { Text += T; }
                else { return Failed; }
            }
        }
        else { return Failed; } // No built-in code, browser, shell, or remote MCP actions.
    }
    auto Normalized = MakeShared<FJsonObject>(); auto Choice = MakeShared<FJsonObject>(); auto M = MakeShared<FJsonObject>();
    M->SetStringField(TEXT("role"), TEXT("assistant")); M->SetStringField(TEXT("content"), Text);
    if (!Calls.IsEmpty()) { M->SetArrayField(TEXT("tool_calls"), Calls); }
    Choice->SetObjectField(TEXT("message"), M); Choice->SetStringField(TEXT("finish_reason"), Calls.IsEmpty() ? TEXT("stop") : TEXT("tool_calls"));
    Normalized->SetArrayField(TEXT("choices"), {MakeShared<FJsonValueObject>(Choice)});
    const TSharedPtr<FJsonObject>* Usage = nullptr;
    if (O->TryGetObjectField(TEXT("usage"), Usage))
    {
        auto U = MakeShared<FJsonObject>();
        for (const auto& Pair : {TPair<FString, FString>(TEXT("input_tokens"), TEXT("prompt_tokens")), TPair<FString, FString>(TEXT("output_tokens"), TEXT("completion_tokens"))})
        { if (const auto Value = (*Usage)->TryGetField(Pair.Key)) { U->SetField(Pair.Value, Value); } }
        const TSharedPtr<FJsonObject>* Details = nullptr;
        if ((*Usage)->TryGetObjectField(TEXT("input_tokens_details"), Details)) { U->SetObjectField(TEXT("prompt_tokens_details"), Details->ToSharedRef()); }
        Normalized->SetObjectField(TEXT("usage"), U);
    }
    if (const auto Credits = O->TryGetField(TEXT("credits"))) { Normalized->SetField(TEXT("credits"), Credits); }
    auto R = KHUChatProtocol::Parse(KHUMcpProtocol::WriteObject(Normalized));
    if (R.bSuccess) { R.Assistant->SetArrayField(TEXT("_responses_output"), *Output); }
    return R;
}

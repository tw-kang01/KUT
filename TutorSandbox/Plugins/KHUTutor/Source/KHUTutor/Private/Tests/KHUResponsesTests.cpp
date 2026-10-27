#include "AI/KHUChatClient.h"
#include "AI/KHUAgentLoop.h"
#include "KHUTutorModule.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FString First = TEXT("{\"status\":\"completed\",\"output\":[{\"type\":\"reasoning\",\"id\":\"rs_fixture\",\"summary\":[],\"encrypted_content\":\"fixture_encrypted\"},{\"type\":\"function_call\",\"id\":\"fc_fixture\",\"call_id\":\"call_fixture\",\"name\":\"KHUTutorTools_KHUTutorReadToolset_Ping\",\"arguments\":\"{\\\"message\\\":\\\"responses-fixture\\\"}\"}],\"usage\":{\"input_tokens\":100,\"output_tokens\":20,\"input_tokens_details\":{\"cached_tokens\":10}}}");
    const FString Final = TEXT("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"TutorSandbox 연결 확인\"}]}],\"usage\":{\"input_tokens\":200,\"output_tokens\":30,\"input_tokens_details\":{\"cached_tokens\":0}}}");
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesParse, "KHUTutor.AI.Responses.ParseAndUsage", Flags)
bool FResponsesParse::RunTest(const FString&)
{
    const auto R = KHUResponsesProtocol::Parse(First);
    TestTrue(TEXT("Completed response parsed"), R.bSuccess); TestEqual(TEXT("One owned function call"), R.Calls.Num(), 1);
    if (R.Calls.Num() == 1) { TestEqual(TEXT("call_id, not item id"), R.Calls[0].Id, FString(TEXT("call_fixture"))); }
    TestTrue(TEXT("Usage known"), R.bUsageKnown); TestEqual(TEXT("Input"), R.InputTokens, int64(100)); TestEqual(TEXT("Cached"), R.CachedTokens, int64(10));
    TestEqual(TEXT("Reasoning and function item retained"), R.Assistant->GetArrayField(TEXT("_responses_output")).Num(), 2);
    const auto End = KHUResponsesProtocol::Parse(Final); TestTrue(TEXT("Final Korean text"), End.bSuccess && End.Content.Contains(TEXT("연결 확인")));
    TestFalse(TEXT("Incomplete calls must not execute"), KHUResponsesProtocol::Parse(First.Replace(TEXT("completed"), TEXT("incomplete"))).bSuccess);
    TestFalse(TEXT("Unknown built-in tools must not execute"), KHUResponsesProtocol::Parse(TEXT("{\"status\":\"completed\",\"output\":[{\"type\":\"code_interpreter_call\"}]}")).bSuccess);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesReplay, "KHUTutor.AI.Responses.StatelessReplay", Flags)
bool FResponsesReplay::RunTest(const FString&)
{
    const auto C = IKHUTutorModule::Get().PanelConfig(); FKHUToolBridge Bridge(C, FKHUToolExecutorDirect::Catalog());
    FKHUChatRequest R; R.Model = TEXT("fixture_model"); R.MaxTokens = 1500; R.ReasoningEffort = TEXT("low"); R.bResponses = true;
    R.Tools = Bridge.Definitions(TEXT("T1"), 1, false);
    R.Messages.Add(MakeShared<FJsonValueObject>(KHUResponsesProtocol::Parse(First).Assistant));
    auto Tool = MakeShared<FJsonObject>(); Tool->SetStringField(TEXT("role"), TEXT("tool")); Tool->SetStringField(TEXT("tool_call_id"), TEXT("call_fixture")); Tool->SetStringField(TEXT("content"), TEXT("PONG: fixture"));
    R.Messages.Add(MakeShared<FJsonValueObject>(Tool)); TSharedPtr<FJsonObject> O;
    TestTrue(TEXT("Body valid JSON"), KHUMcpProtocol::ReadObject(KHUResponsesProtocol::Body(R), O));
    TestFalse(TEXT("Stateless storage"), O->GetBoolField(TEXT("store"))); TestFalse(TEXT("Serial model calls"), O->GetBoolField(TEXT("parallel_tool_calls")));
    TestFalse(TEXT("Legacy max_tokens omitted"), O->HasField(TEXT("max_tokens"))); TestEqual(TEXT("Output budget"), O->GetNumberField(TEXT("max_output_tokens")), 1500.0);
    const auto& Input = O->GetArrayField(TEXT("input")); TestEqual(TEXT("All output items plus result"), Input.Num(), 3);
    if (Input.Num() == 3)
    {
        TestEqual(TEXT("Encrypted reasoning replayed"), Input[0]->AsObject()->GetStringField(TEXT("encrypted_content")), FString(TEXT("fixture_encrypted")));
        TestEqual(TEXT("Correct result shape"), Input[2]->AsObject()->GetStringField(TEXT("type")), FString(TEXT("function_call_output")));
        TestEqual(TEXT("Matched call id"), Input[2]->AsObject()->GetStringField(TEXT("call_id")), FString(TEXT("call_fixture")));
    }
    for (const auto& V : O->GetArrayField(TEXT("tools")))
    { TestFalse(TEXT("Flattened function shape"), V->AsObject()->HasField(TEXT("function"))); TestFalse(TEXT("Explicit non-strict matches existing local validator"), V->AsObject()->GetBoolField(TEXT("strict"))); }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesLoop, "KHUTutor.AI.Responses.LoopAndTransport", Flags)
bool FResponsesLoop::RunTest(const FString&)
{
    class FTransport final : public IKHUMcpTransport
    {
    public:
        TArray<FKHUMcpHttpRequest> Posts;
        virtual FCancel Send(const FKHUMcpHttpRequest& R, FCompletion Callback) override
        {
            FKHUMcpHttpResponse Reply; Reply.Status = 200;
            if (R.Verb == TEXT("GET")) { Reply.Body = TEXT("{\"data\":[{\"id\":\"fixture_model\",\"type\":\"llm\",\"endpoints\":[\"/v1/gateway/responses/\"]}]}"); }
            else { Posts.Add(R); Reply.Body = Posts.Num() == 1 ? First : Final; }
            Callback(MoveTemp(Reply)); return [] {};
        }
    };
    class FExecutor final : public IKHUToolExecutor
    {
    public:
        int32 Count = 0;
        virtual void Execute(const FString& Name, const TSharedRef<FJsonObject>& Args, FCallback Callback) override
        { ++Count; FKHUMcpResult R; R.bSuccess = true; R.Text = TEXT("PONG: responses-fixture | project TutorSandbox"); Callback(MoveTemp(R)); }
        virtual void Cancel() override {}
    };
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Tiers[TEXT("T1")].Chain = {TEXT("fixture_model")}; C.Tiers[TEXT("T1")].Api = TEXT("responses"); C.Prices.Add(TEXT("fixture_model"), C.Prices[TEXT("gpt-6.1-sol")]);
    auto Transport = MakeShared<FTransport>(); auto Executor = MakeShared<FExecutor>();
    auto Chat = MakeShared<FKHUChatClient>(C, TEXT("fixture_api_secret"), Transport);
    auto Loop = MakeShared<FKHUAgentLoop>(C, Chat, Executor, MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    TestTrue(TEXT("Responses loop starts"), Loop->Ask(TEXT("Ping 확인"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    TestEqual(TEXT("One sequential tool"), Executor->Count, 1); TestEqual(TEXT("Two Responses POSTs"), Transport->Posts.Num(), 2);
    if (Transport->Posts.Num() == 2)
    {
        TestTrue(TEXT("Configured endpoint"), Transport->Posts[0].Url.EndsWith(TEXT("/responses/")));
        TSharedPtr<FJsonObject> Second; KHUMcpProtocol::ReadObject(Transport->Posts[1].Body, Second);
        TestTrue(TEXT("Function output and reasoning included"), Transport->Posts[1].Body.Contains(TEXT("fixture_encrypted")) && Transport->Posts[1].Body.Contains(TEXT("function_call_output")));
    }
    TestEqual(TEXT("Task completed"), Loop->State(), EKHUAgentState::Idle);
    TestTrue(TEXT("Korean final answer"), Loop->Receipt()->GetStringField(TEXT("text")).Contains(TEXT("연결 확인"))); Loop->Cancel(); return true;
}
#endif

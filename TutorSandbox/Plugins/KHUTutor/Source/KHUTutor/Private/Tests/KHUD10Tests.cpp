#include "AI/KHUAgentLoop.h"
#include "AI/KHUContextBudget.h"
#include "KHUTutorModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonValue> M(const TCHAR* Role, const FString& Text)
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("role"), Role); O->SetStringField(TEXT("content"), Text);
        return MakeShared<FJsonValueObject>(O);
    }
    void Evidence(FKHUChatRequest& R, const FString& Id, const FString& Text)
    {
        auto A = M(TEXT("assistant"), TEXT(""))->AsObject(); auto C = MakeShared<FJsonObject>(), F = MakeShared<FJsonObject>();
        C->SetStringField(TEXT("id"), Id); C->SetStringField(TEXT("type"), TEXT("function"));
        F->SetStringField(TEXT("name"), TEXT("KHUTutorTools_KHUTutorReadToolset_GetBlueprintGraphDigest")); F->SetStringField(TEXT("arguments"), TEXT("{}"));
        C->SetObjectField(TEXT("function"), F); A->SetArrayField(TEXT("tool_calls"), {MakeShared<FJsonValueObject>(C)});
        if (R.bResponses)
        {
            auto Reason = MakeShared<FJsonObject>(); Reason->SetStringField(TEXT("type"), TEXT("reasoning")); Reason->SetStringField(TEXT("encrypted_content"), TEXT("PRESERVE_ENCRYPTED_REASONING"));
            auto Raw = MakeShared<FJsonObject>(); Raw->SetStringField(TEXT("type"), TEXT("function_call")); Raw->SetStringField(TEXT("call_id"), Id);
            Raw->SetStringField(TEXT("name"), F->GetStringField(TEXT("name"))); Raw->SetStringField(TEXT("arguments"), TEXT("{}"));
            A->SetArrayField(TEXT("_responses_output"), {MakeShared<FJsonValueObject>(Reason), MakeShared<FJsonValueObject>(Raw)});
        }
        R.Messages.Add(MakeShared<FJsonValueObject>(A)); auto T = M(TEXT("tool"), Text)->AsObject();
        T->SetStringField(TEXT("tool_call_id"), Id); R.Messages.Add(MakeShared<FJsonValueObject>(T));
    }
    FString Body(const FKHUChatRequest& R) { return R.bResponses ? KHUResponsesProtocol::Body(R) : KHUChatProtocol::Body(R); }
    class FNoTools final : public IKHUToolExecutor
    {
    public:
        virtual void Execute(const FString&, const TSharedRef<FJsonObject>&, FCallback Callback) override { Callback(FKHUMcpResult()); }
        virtual void Cancel() override {}
    };
    class FChat final : public IKHUChatClient
    {
    public:
        TArray<FKHUChatRequest> Requests; TArray<FCallback> Callbacks;
        bool Deferred = false, Mock = false, UsageKnown = true;
        int32 Attempts = 1;
        FKHUChatResponse Answer(const FKHUChatRequest& R) const
        {
            FKHUChatResponse A; A.bSuccess = true; A.bMock = Mock; A.Attempts = Attempts; A.bUsageKnown = UsageKnown;
            A.InputTokens = 100; A.OutputTokens = 1; A.InputPayloadBytes = FKHUContextBudgeter::Utf8Bytes(Body(R));
            A.Content = TEXT("실제 네트워크 없이 재현한 답변"); A.Assistant = M(TEXT("assistant"), A.Content)->AsObject(); return A;
        }
        virtual void Send(FKHUChatRequest R, FCallback Callback) override
        {
            Requests.Add(R);
            if (Deferred) { Callbacks.Add(MoveTemp(Callback)); }
            else { Callback(Answer(R)); }
        }
        virtual void Cancel() override {} // Deliberately deliver a late callback to exercise the generation guard.
        virtual bool IsMock() const override { return Mock; }
    };
    FKHUConfig BudgetConfig()
    {
        auto C = IKHUTutorModule::Get().PanelConfig(); C.Tiers[TEXT("T1")].Chain = {TEXT("d10_fixture_model")};
        C.Tiers[TEXT("T1")].Api = TEXT("chat_completions"); C.Tiers[TEXT("T1")].ReasoningEffort.Reset();
        FKHUPrice P; P.Input = P.CachedInput = 0; P.Output = 1; P.CreditsPerUsd = 1000000;
        C.Prices.Add(TEXT("d10_fixture_model"), P); C.OutputHardLimit = 1; C.PerTaskCredits = 1; C.PerTaskHardCredits = 2;
        return C;
    }
    TSharedRef<FKHUAgentLoop> Loop(const FKHUConfig& C, TSharedRef<FChat> Chat)
    { return MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FNoTools>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog())); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Estimate, "KHUTutor.Context.Utf8Calibration", Flags)
bool FD10Estimate::RunTest(const FString&)
{
    const auto P = IKHUTutorModule::Get().PanelConfig().Context; FKHUTokenEstimator E(P);
    TestEqual(TEXT("Korean uses UTF-8 byte count"), FKHUContextBudgeter::Utf8Bytes(TEXT("가")), 3);
    TestEqual(TEXT("Surrogate pair uses four bytes"), FKHUContextBudgeter::Utf8Bytes(TEXT("😀")), 4);
    TestTrue(TEXT("Byte and token units differ"), E.Estimate(TEXT("fixture"), 1000) < 1000);
    TestFalse(TEXT("Unknown/zero usage never trains"), E.Observe(TEXT("fixture"), 1000, 0));
    TestFalse(TEXT("Invalid body never trains"), E.Observe(TEXT("fixture"), 0, 200));
    TestTrue(TEXT("Known usage trains EMA"), E.Observe(TEXT("fixture"), 1000, 800));
    TestTrue(TEXT("EMA is bounded and not replaced by one sample"), E.Ratio(TEXT("fixture")) > P.TokensPerByte && E.Ratio(TEXT("fixture")) < 0.8);
    TestEqual(TEXT("Model samples separate"), E.Samples(TEXT("other")), 0);
    TestTrue(TEXT("Outlier remains bounded"), E.Observe(TEXT("fixture"), 1, 1000000));
    TestTrue(TEXT("Ratio clamped"), E.Ratio(TEXT("fixture")) <= P.MaxRatio); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Persistence, "KHUTutor.Context.CalibrationPersistence", Flags)
bool FD10Persistence::RunTest(const FString&)
{
    const auto P = IKHUTutorModule::Get().PanelConfig().Context;
    const FString Path = FPaths::ProjectSavedDir() / TEXT("KHUTutor/D10") / (FGuid::NewGuid().ToString() + TEXT(".json"));
    FKHUTokenEstimator A(P, Path), B(P, Path);
    TestTrue(TEXT("First model saved"), A.Observe(TEXT("model_a"), 1000, 400));
    TestTrue(TEXT("Second panel merges instead of overwriting"), B.Observe(TEXT("model_b"), 1000, 600));
    FKHUTokenEstimator Reloaded(P, Path);
    TestEqual(TEXT("First model retained"), Reloaded.Samples(TEXT("model_a")), 1); TestEqual(TEXT("Second model retained"), Reloaded.Samples(TEXT("model_b")), 1);
    FString Text; FFileHelper::LoadFileToString(Text, *Path);
    TestFalse(TEXT("No request content persisted"), Text.Contains(TEXT("content")) || Text.Contains(TEXT("messages")) || Text.Contains(TEXT("Bearer")));
    FFileHelper::SaveStringToFile(TEXT("broken JSON"), *Path); FKHUTokenEstimator Corrupt(P, Path);
    TestEqual(TEXT("Corrupt calibration falls back to configured ratio"), Corrupt.Ratio(TEXT("model_a")), P.TokensPerByte); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Atomic, "KHUTutor.Context.AtomicHistoryBothApis", Flags)
bool FD10Atomic::RunTest(const FString&)
{
    const auto C = IKHUTutorModule::Get().PanelConfig(); auto P = C.Context; P.RecentTurns = 1;
    for (bool Responses : {false, true})
    {
        FKHUChatRequest R; R.Model = TEXT("fixture"); R.bResponses = Responses; R.MaxTokens = 100;
        R.Messages = {M(TEXT("system"), TEXT("PRESERVE_SYSTEM_RULES")), M(TEXT("user"), TEXT("ORIGINAL_TASK")), M(TEXT("assistant"), FString::ChrN(10000, 'x')),
            M(TEXT("user"), TEXT("READ_GRAPH"))};
        Evidence(R, TEXT("call_current"), TEXT("GRAPH /Game/Door #2 Target=!UNCONNECTED ERR missing Target"));
        R.Messages.Add(M(TEXT("assistant"), TEXT("위치 힌트"))); R.Messages.Add(M(TEXT("user"), TEXT("CURRENT_QUESTION 더 설명해주세요")));
        const auto Old = R.Messages[2]->AsObject(); const auto Result = FKHUContextBudgeter::Build(R, P, FKHUTokenEstimator(P), 2500);
        TestTrue(TEXT("Bounded context built"), Result.bSuccess);
        if (!Result.bSuccess) { continue; }
        const FString Out = Body(R);
        TestTrue(TEXT("Only complete old history omitted"), Result.OmittedTurns > 0 && FKHUContextBudgeter::Validate(R.Messages));
        TestTrue(TEXT("Intent, current question, system, graph retained"), Out.Contains(TEXT("ORIGINAL_TASK")) && Out.Contains(TEXT("CURRENT_QUESTION")) && Out.Contains(TEXT("PRESERVE_SYSTEM_RULES")) && Out.Contains(TEXT("#2 Target=!UNCONNECTED")));
        TestTrue(TEXT("Omission visible"), Out.Contains(TEXT("KHU_CONTEXT_OMITTED")));
        TestEqual(TEXT("Original shared text not mutated"), Old->GetStringField(TEXT("content")).Len(), 10000);
        TestTrue(TEXT("Graph segment counted"), Result.Segments->GetObjectField(TEXT("ctx_graph"))->GetNumberField(TEXT("utf8_bytes")) > 0);
        if (Responses) { TestTrue(TEXT("Encrypted reasoning replayed intact"), Out.Contains(TEXT("PRESERVE_ENCRYPTED_REASONING")) && Out.Contains(TEXT("function_call_output"))); }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Pinned, "KHUTutor.Context.ProtectedOverflow", Flags)
bool FD10Pinned::RunTest(const FString&)
{
    const auto P = IKHUTutorModule::Get().PanelConfig().Context;
    FKHUChatRequest R; R.Model = TEXT("fixture"); R.MaxTokens = 100;
    R.Messages = {M(TEXT("system"), TEXT("RULES")), M(TEXT("user"), FString::ChrN(3000, TEXT("가")[0]))};
    const FString Before = Body(R);
    TestFalse(TEXT("Cannot cut protected question to fit"), FKHUContextBudgeter::Build(R, P, FKHUTokenEstimator(P), 100).bSuccess);
    TestEqual(TEXT("Failure leaves request untouched"), Body(R), Before);
    auto SmallWire = P; SmallWire.MaxBytes = 1024;
    TestFalse(TEXT("Wire bytes bound independent of generous token limit"), FKHUContextBudgeter::Build(R, SmallWire, FKHUTokenEstimator(SmallWire), 100000).bSuccess);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Pairing, "KHUTutor.Context.InvalidToolPairs", Flags)
bool FD10Pairing::RunTest(const FString&)
{
    FKHUChatRequest R; R.Model = TEXT("fixture"); R.Messages = {M(TEXT("user"), TEXT("question"))}; Evidence(R, TEXT("id"), TEXT("result"));
    TestTrue(TEXT("Complete pair accepted"), FKHUContextBudgeter::Validate(R.Messages));
    auto Missing = R.Messages; Missing.Pop(); TestFalse(TEXT("Dangling call rejected"), FKHUContextBudgeter::Validate(Missing));
    auto Orphan = R.Messages; Orphan.RemoveAt(1); TestFalse(TEXT("Orphan result rejected"), FKHUContextBudgeter::Validate(Orphan));
    auto Duplicate = R.Messages; Duplicate.Add(R.Messages.Last()); TestFalse(TEXT("Duplicate result rejected"), FKHUContextBudgeter::Validate(Duplicate));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Budget, "KHUTutor.Budget.SoftHardContinue", Flags)
bool FD10Budget::RunTest(const FString&)
{
    auto C = BudgetConfig(); auto Chat = MakeShared<FChat>(); const auto Agent = Loop(C, Chat);
    TestTrue(TEXT("First request fits one-credit allowance"), Agent->Ask(TEXT("first"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    TestEqual(TEXT("First paid fixture request sent"), Chat->Requests.Num(), 1);
    TestTrue(TEXT("Follow-up retained but pauses"), Agent->FollowUp(TEXT("second"), 1));
    TestTrue(TEXT("Soft pause can continue"), Agent->CanResume()); TestEqual(TEXT("No request before soft approval"), Chat->Requests.Num(), 1);
    Agent->Resume(); TestEqual(TEXT("One more request after Continue"), Chat->Requests.Num(), 2);
    Agent->FollowUp(TEXT("third"), 1); TestFalse(TEXT("Hard pause cannot continue"), Agent->CanResume());
    Agent->Resume(); TestEqual(TEXT("No request above hard limit"), Chat->Requests.Num(), 2);
    TestEqual(TEXT("Hard budget cannot grow"), Agent->Receipt()->GetNumberField(TEXT("task_allowance")), 2.0);
    TestEqual(TEXT("Correct pause reason"), Agent->Receipt()->GetStringField(TEXT("pause_reason")), FString(TEXT("hard_budget"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Iteration, "KHUTutor.Budget.IterationContinueKeepsAllowance", Flags)
bool FD10Iteration::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Tiers[TEXT("T1")].MaxIters = 1;
    class FRead final : public IKHUToolExecutor
    {
    public:
        virtual void Execute(const FString&, const TSharedRef<FJsonObject>&, FCallback Callback) override
        { FKHUMcpResult R; R.bSuccess = true; R.Text = TEXT("GRAPH #2 Target=!UNCONNECTED"); Callback(MoveTemp(R)); }
        virtual void Cancel() override {}
    };
    // Recorded mock remains asynchronous; another test below checks the synchronous budget boundaries.
    auto Chat = FKHUMockChatClient::Load(C.FixtureDirectory / TEXT("scenario_b.json"));
    auto Agent = MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FRead>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    Agent->Ask(TEXT("graph"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FCheck(FAutomationTestBase* T, TSharedRef<FKHUAgentLoop> A, double Credits) : Test(T), Agent(A), Initial(Credits) {}
        virtual bool Update() override
        {
            if (Agent->State() == EKHUAgentState::WaitingModel && FPlatformTime::Seconds() < Deadline) { return false; }
            Test->TestTrue(TEXT("Iteration pause can resume"), Agent->CanResume()); Agent->Resume();
            Test->TestEqual(TEXT("Iteration Continue grants no credits"), Agent->Receipt()->GetNumberField(TEXT("task_allowance")), Initial);
            Agent->Cancel(); return true;
        }
        FAutomationTestBase* Test; TSharedRef<FKHUAgentLoop> Agent; double Initial; double Deadline = FPlatformTime::Seconds() + 10;
    };
    AddCommand(new FCheck(this, Agent, C.PerTaskCredits)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Reserve, "KHUTutor.Budget.KnownFinalUsageCountedOnce", Flags)
bool FD10Reserve::RunTest(const FString&)
{
    auto C = BudgetConfig(); C.PerTaskCredits = 10; C.PerTaskHardCredits = 20;
    auto Chat = MakeShared<FChat>(); Chat->Attempts = 2; auto Agent = Loop(C, Chat);
    Agent->Ask(TEXT("retry"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    const auto Receipt = Agent->Receipt(); const auto U = Receipt->GetArrayField(TEXT("usage"))[0]->AsObject();
    TestEqual(TEXT("Known final cost counted once"), U->GetNumberField(TEXT("credits_estimate")), 1.0);
    TestEqual(TEXT("Only unknown prior attempt reserved"), U->GetNumberField(TEXT("unverified_attempt_reserve")), 1.0);
    TestEqual(TEXT("Budget debited known plus uncertain, not twice final"), Receipt->GetNumberField(TEXT("budget_reserved_or_estimated")), 2.0);
    TestFalse(TEXT("Uncertain retry spend not marked verified"), Receipt->GetBoolField(TEXT("cost_known"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Unknown, "KHUTutor.Budget.UnknownUsageNotZero", Flags)
bool FD10Unknown::RunTest(const FString&)
{
    auto C = BudgetConfig(); auto Chat = MakeShared<FChat>(); Chat->UsageKnown = false; auto Agent = Loop(C, Chat);
    Agent->Ask(TEXT("unknown usage"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    const auto Receipt = Agent->Receipt(); const auto U = Receipt->GetArrayField(TEXT("usage"))[0]->AsObject();
    TestTrue(TEXT("Unknown cost is null"), U->TryGetField(TEXT("credits_estimate"))->Type == EJson::Null);
    TestEqual(TEXT("Unknown attempt has reserve"), U->GetNumberField(TEXT("unverified_attempt_reserve")), 1.0);
    TestFalse(TEXT("Not silently reported free"), Receipt->GetBoolField(TEXT("cost_known"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Task, "KHUTutor.Budget.CancelledTaskAttribution", Flags)
bool FD10Task::RunTest(const FString&)
{
    auto C = BudgetConfig(); auto Chat = MakeShared<FChat>(); Chat->Deferred = true; auto Agent = Loop(C, Chat);
    Agent->Ask(TEXT("old task"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    const FString OldId = Agent->Receipt()->GetStringField(TEXT("task_id")); Agent->Cancel();
    Agent->Ask(TEXT("new task"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    const FString NewId = Agent->Receipt()->GetStringField(TEXT("task_id"));
    auto OldCallback = MoveTemp(Chat->Callbacks[0]); OldCallback(Chat->Answer(Chat->Requests[0]));
    TestEqual(TEXT("Late old result never charged to new usage"), Agent->Receipt()->GetArrayField(TEXT("usage")).Num(), 0);
    auto NewCallback = MoveTemp(Chat->Callbacks[1]); NewCallback(Chat->Answer(Chat->Requests[1]));
    const auto Receipt = Agent->Receipt();
    TestNotEqual(TEXT("Task identities differ"), OldId, NewId);
    TestEqual(TEXT("Current usage belongs to new task"), Receipt->GetArrayField(TEXT("usage"))[0]->AsObject()->GetStringField(TEXT("task_id")), NewId);
    TestEqual(TEXT("Canceled uncertainty belongs to old task"), Receipt->GetArrayField(TEXT("cancelled_requests_billing_unknown"))[0]->AsObject()->GetStringField(TEXT("task_id")), OldId);
    TestEqual(TEXT("No old spend charged to new task"), Receipt->GetNumberField(TEXT("budget_reserved_or_estimated")), 1.0); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10Segments, "KHUTutor.Context.RequestSegmentsAndT1", Flags)
bool FD10Segments::RunTest(const FString&)
{
    const auto C = IKHUTutorModule::Get().PanelConfig(); auto Chat = MakeShared<FChat>(); Chat->Mock = true;
    auto Agent = Loop(C, Chat); Agent->Ask(TEXT("현재 오류"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    TestEqual(TEXT("Existing T1 model retained"), Chat->Requests[0].Model, C.Tiers[TEXT("T1")].Chain[0]);
    const auto Context = Agent->Receipt()->GetArrayField(TEXT("context"))[0]->AsObject();
    TestTrue(TEXT("Wire bytes and estimated tokens reported separately"), Context->GetNumberField(TEXT("input_utf8_bytes")) != Context->GetNumberField(TEXT("estimated_input_tokens")));
    const auto S = Context->GetObjectField(TEXT("segments"));
    TestTrue(TEXT("System, tools and user segments present"), S->GetObjectField(TEXT("system"))->GetNumberField(TEXT("utf8_bytes")) > 0 && S->GetObjectField(TEXT("tools"))->GetNumberField(TEXT("utf8_bytes")) > 0 && S->GetObjectField(TEXT("user"))->GetNumberField(TEXT("utf8_bytes")) > 0);
    TestEqual(TEXT("No free automatic answer branch"), Chat->Requests.Num(), 1); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10ConfigBounds, "KHUTutor.Context.ConfigBoundsFailClosed", Flags)
bool FD10ConfigBounds::RunTest(const FString&)
{
    const FString Source = IPluginManager::Get().FindPlugin(TEXT("KHUTutor"))->GetBaseDir() / TEXT("Config");
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("KHUTutor/D10") / FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*(Dir / TEXT("Prompts")), true);
    for (const TCHAR* Name : {TEXT("Models.json"), TEXT("Pricing.json"), TEXT("Budget.json"), TEXT("Context.json"), TEXT("Gateway.json"), TEXT("DevelopmentBudget.json"), TEXT("ToolWhitelist.json"), TEXT("ErrorPatterns.json"), TEXT("Prompts/system.txt"), TEXT("Prompts/l0_contract.txt")})
    {
        FString Text; FFileHelper::LoadFileToString(Text, *(Source / Name)); FFileHelper::SaveStringToFile(Text, *(Dir / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    TestTrue(TEXT("Valid copied policy loads"), FKHUConfig::Load(Dir).bValid);
    FString Original; FFileHelper::LoadFileToString(Original, *(Dir / TEXT("Context.json"))); TSharedPtr<FJsonObject> O;
    KHUMcpProtocol::ReadObject(Original, O); O->SetNumberField(TEXT("max_messages"), 3);
    FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(O.ToSharedRef()), *(Dir / TEXT("Context.json")));
    TestFalse(TEXT("Unsafe context bound disables paid calls"), FKHUConfig::Load(Dir).bValid);
    FFileHelper::SaveStringToFile(Original, *(Dir / TEXT("Context.json")));
    FString Budget; FFileHelper::LoadFileToString(Budget, *(Dir / TEXT("Budget.json"))); KHUMcpProtocol::ReadObject(Budget, O);
    O->SetNumberField(TEXT("per_task_hard"), O->GetNumberField(TEXT("per_task_soft")) - 1);
    FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(O.ToSharedRef()), *(Dir / TEXT("Budget.json")));
    TestFalse(TEXT("Hard below soft disables paid calls"), FKHUConfig::Load(Dir).bValid); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD10HttpProtected, "KHUTutor.Context.Http413KeepsProtectedInput", Flags)
bool FD10HttpProtected::RunTest(const FString&)
{
    class FTransport final : public IKHUMcpTransport
    {
    public:
        TArray<FKHUMcpHttpRequest> Requests;
        virtual FCancel Send(const FKHUMcpHttpRequest& Request, FCompletion Completion) override
        {
            Requests.Add(Request); FKHUMcpHttpResponse R;
            R.Status = Requests.Num() == 1 ? 200 : 413;
            if (R.Status == 200) { R.Body = TEXT("{\"data\":[{\"id\":\"d10_fixture_model\",\"type\":\"llm\",\"endpoints\":[\"/v1/gateway/chat/completions/\"]}]}"); }
            AsyncTask(ENamedThreads::GameThread, [R = MoveTemp(R), Completion = MoveTemp(Completion)]() mutable { Completion(MoveTemp(R)); });
            return []() {};
        }
    };
    struct FState
    {
        bool Done = false; FKHUChatResponse Response; TSharedPtr<FKHUChatClient> Http; TSharedPtr<FTransport> Transport;
        TSharedPtr<FJsonObject> Question; double Deadline = FPlatformTime::Seconds() + 10;
    };
    auto S = MakeShared<FState>(); S->Transport = MakeShared<FTransport>();
    S->Http = MakeShared<FKHUChatClient>(BudgetConfig(), TEXT("fixture_secret_123456"), S->Transport.ToSharedRef());
    FKHUChatRequest R; R.Model = TEXT("d10_fixture_model"); R.MaxTokens = R.OutputHardLimit = 1; R.bUnlimitedDevelopment = true;
    S->Question = M(TEXT("user"), TEXT("현재 질문 원문을 그대로 보존하세요"))->AsObject(); R.Messages = {MakeShared<FJsonValueObject>(S->Question)};
    S->Http->Send(R, [S](FKHUChatResponse Reply) { S->Response = MoveTemp(Reply); S->Done = true; });
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FCheck(FAutomationTestBase* T, TSharedRef<FState> In) : Test(T), State(In) {}
        virtual bool Update() override
        {
            if (!State->Done && FPlatformTime::Seconds() < State->Deadline) { return false; }
            Test->TestTrue(TEXT("413 finished within deadline"), State->Done);
            Test->TestFalse(TEXT("Protected overflow is an honest failure"), State->Response.bSuccess);
            Test->TestEqual(TEXT("No destructive second POST"), State->Transport->Requests.Num(), 2);
            Test->TestEqual(TEXT("Question never mutated"), State->Question->GetStringField(TEXT("content")), FString(TEXT("현재 질문 원문을 그대로 보존하세요")));
            State->Http->Cancel(); return true;
        }
        FAutomationTestBase* Test; TSharedRef<FState> State;
    };
    AddCommand(new FCheck(this, S)); return true;
}
#endif

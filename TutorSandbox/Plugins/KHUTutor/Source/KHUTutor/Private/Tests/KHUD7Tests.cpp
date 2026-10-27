#include "AI/KHUAgentLoop.h"
#include "AI/KHUSecretStore.h"
#include "KHUTutorModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FKHUConfig Config() { return FKHUConfig::Load(IPluginManager::Get().FindPlugin(TEXT("KHUTutor"))->GetBaseDir() / TEXT("Config")); }
    FString Answer(const FString& Text)
    {
        auto Message = MakeShared<FJsonObject>(); Message->SetStringField(TEXT("role"), TEXT("assistant")); Message->SetStringField(TEXT("content"), Text);
        auto Choice = MakeShared<FJsonObject>(); Choice->SetObjectField(TEXT("message"), Message);
        auto O = MakeShared<FJsonObject>(); O->SetArrayField(TEXT("choices"), {MakeShared<FJsonValueObject>(Choice)}); return KHUMcpProtocol::WriteObject(O);
    }
    class FExecutor final : public IKHUToolExecutor, public TSharedFromThis<FExecutor>
    {
    public:
        TArray<FString> Names; bool Fail = false; uint64 Generation = 0;
        virtual void Execute(const FString& Native, const TSharedRef<FJsonObject>& Args, FCallback Callback) override
        {
            Names.Add(Native); const uint64 G = Generation; const TWeakPtr<FExecutor> Weak = AsShared();
            AsyncTask(ENamedThreads::GameThread, [Weak, G, Callback = MoveTemp(Callback)]() mutable
            { if (auto S = Weak.Pin(); S && S->Generation == G) { FKHUMcpResult R; R.bSuccess = !S->Fail; R.Text = TEXT("GRAPH /Game/KHUTest/BP_Door_Broken: #2 Target=!UNCONNECTED"); R.Error = TEXT("ERROR: fixture"); Callback(MoveTemp(R)); } });
        }
        virtual void Cancel() override { ++Generation; }
    };
    class FTransport final : public IKHUMcpTransport, public TSharedFromThis<FTransport>
    {
    public:
        TArray<FKHUMcpHttpRequest> Requests; TArray<FKHUMcpHttpResponse> Replies; uint64 Generation = 0;
        virtual FCancel Send(const FKHUMcpHttpRequest& Request, FCompletion Completion) override
        {
            Requests.Add(Request); FKHUMcpHttpResponse R;
            if (!Replies.IsEmpty()) { R = MoveTemp(Replies[0]); Replies.RemoveAt(0); }
            const uint64 G = Generation; const TWeakPtr<FTransport> Weak = AsShared();
            AsyncTask(ENamedThreads::GameThread, [Weak, G, R = MoveTemp(R), Completion = MoveTemp(Completion)]() mutable
            { if (auto S = Weak.Pin(); S && S->Generation == G) { Completion(MoveTemp(R)); } });
            return [Weak]() { if (auto S = Weak.Pin()) { ++S->Generation; } };
        }
    };
    FKHUMcpHttpResponse Reply(int32 Code, const FString& Body = TEXT("")) { FKHUMcpHttpResponse R; R.Status = Code; R.Body = Body; return R; }
    struct FState
    {
        double Deadline = FPlatformTime::Seconds() + 12; bool Done = false; FString Text;
        TSharedPtr<FKHUAgentLoop> Loop; TSharedPtr<FExecutor> Executor; TSharedPtr<FKHUChatClient> Http; TSharedPtr<FTransport> Transport;
        FKHUChatResponse Response; FKHUMcpResult ToolResult; bool CanceledCallback = false;
    };
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FCheck(FAutomationTestBase* InTest, TSharedRef<FState> InState, TFunction<void()> InCheck) : Test(InTest), State(InState), Check(MoveTemp(InCheck)) {}
        virtual bool Update() override
        {
            if (!State->Done && FPlatformTime::Seconds() < State->Deadline) { return false; }
            Test->TestTrue(TEXT("Asynchronous operation completed"), State->Done); if (State->Done) { Check(); }
            if (State->Done && State->Loop) { State->Loop->Cancel(); } if (State->Http) { State->Http->Cancel(); } return true;
        }
    private:
        FAutomationTestBase* Test; TSharedRef<FState> State; TFunction<void()> Check;
    };
    TSharedRef<FState> RunScenario(const FString& Name, FKHUConfig C = Config(), bool bFail = false)
    {
        auto S = MakeShared<FState>(); S->Executor = MakeShared<FExecutor>(); S->Executor->Fail = bFail;
        auto Chat = FKHUMockChatClient::Load(C.FixtureDirectory / (Name + TEXT(".json")));
        S->Loop = MakeShared<FKHUAgentLoop>(C, Chat, S->Executor.ToSharedRef(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
        S->Loop->Ask(TEXT("오류를 확인해 주세요"), TEXT("T1"), 1, [S](EKHUAgentState State, const FString& Text)
        { if (State == EKHUAgentState::Idle || State == EKHUAgentState::AskContinue) { S->Done = true; S->Text = Text; } }); return S;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Config, "KHUTutor.AI.ConfigFailClosed", Flags)
bool FD7Config::RunTest(const FString& Parameters)
{
    const auto C = Config(); TestTrue(TEXT("Plugin config complete"), C.bValid);
    TestTrue(TEXT("Student models keep Opus disabled"), !C.IsModelEnabled(C.Tiers[TEXT("D7")].Chain[0]));
    TestTrue(TEXT("Explicit development authorization allows Opus"), C.IsModelEnabled(C.Tiers[TEXT("D7")].Chain[0], true));
    TestTrue(TEXT("Unknown prices remain unknown"), !C.Cost(TEXT("not_a_model"), 10, 0, 5).IsSet());
    TestFalse(TEXT("Missing config disables real calls"), FKHUConfig::Load(FPaths::ProjectSavedDir() / TEXT("KHUTutor/NoConfig")).bValid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Secret, "KHUTutor.AI.DpapiSecret", Flags)
bool FD7Secret::RunTest(const FString& Parameters)
{
    const FString Path = FPaths::ProjectSavedDir() / TEXT("KHUTutor/D7/secret_test.dpapi"); const FKHUSecretStore Store(Path);
    const FString Key = TEXT("fixture_secret_12345678"); FString Error, Loaded;
    TestTrue(TEXT("DPAPI save"), Store.Save(Key, Error)); TestTrue(TEXT("DPAPI load without environment"), Store.Load(Loaded, Error, false)); TestEqual(TEXT("Same key"), Loaded, Key);
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes, *Path); const FTCHARToUTF8 Plain(*Key);
    TestFalse(TEXT("Ciphertext differs from key bytes"), Bytes.Num() == Plain.Length() && FMemory::Memcmp(Bytes.GetData(), Plain.Get(), Plain.Length()) == 0);
    TestFalse(TEXT("Header injection rejected"), FKHUSecretStore::IsValidKey(TEXT("fixture_key\r\nx-api-key: bad")));
    TestEqual(TEXT("Redaction"), FKHUSecretStore::Redact(TEXT("prefix ") + Key, Key), FString(TEXT("prefix [API_KEY_REDACTED]")));
    FFileHelper::SaveStringToFile(TEXT("corrupt"), *Path); TestFalse(TEXT("Corrupt DPAPI never falls back to env"), Store.Load(Loaded, Error, false));
    TestTrue(TEXT("Owned test file deleted"), Store.Delete(Error)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Bridge, "KHUTutor.AI.BridgePolicy", Flags)
bool FD7Bridge::RunTest(const FString& Parameters)
{
    auto C = Config(); const auto Catalog = FKHUToolExecutorDirect::Catalog(); TestEqual(TEXT("Actual native catalog"), Catalog.Num(), 12);
    FKHUToolBridge Bridge(C, Catalog); TestEqual(TEXT("Read only by default"), Bridge.Definitions(TEXT("T1"), 1).Num(), 9);
    TestEqual(TEXT("Explicit Show permission"), Bridge.Definitions(TEXT("T2"), 3, true).Num(), 12);
    FString Native, Error; TSharedPtr<FJsonObject> Args;
    TestFalse(TEXT("Python never resolves"), Bridge.Resolve(TEXT("execute_python"), TEXT("{}"), TEXT("T2"), 4, true, Native, Args, Error));
    const FString Ping = FKHUToolBridge::FunctionName(FKHUMcpClient::PingToolName());
    TestTrue(TEXT("Exact allowed name"), Bridge.Resolve(Ping, TEXT("{\"message\":\"hi\"}"), TEXT("T1"), 1, false, Native, Args, Error));
    TestFalse(TEXT("Incorrect parameter case"), Bridge.Resolve(Ping, TEXT("{\"Message\":\"hi\"}"), TEXT("T1"), 1, false, Native, Args, Error));
    TestFalse(TEXT("Unknown arguments"), Bridge.Resolve(Ping, TEXT("{\"message\":\"hi\",\"shell\":\"bad\"}"), TEXT("T1"), 1, false, Native, Args, Error));
    const FString Long = FString::ChrN(90, 'A') + TEXT(".한글"); TestEqual(TEXT("Long name capped"), FKHUToolBridge::FunctionName(Long).Len(), 64);
    TestEqual(TEXT("Stable name"), FKHUToolBridge::FunctionName(Long), FKHUToolBridge::FunctionName(Long));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Compression, "KHUTutor.AI.SchemaAndCompression", Flags)
bool FD7Compression::RunTest(const FString& Parameters)
{
    auto C = Config(); auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("name"), TEXT("fixture.a"));
    auto Schema = MakeShared<FJsonObject>(); Schema->SetStringField(TEXT("type"), TEXT("object")); Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
    Schema->SetStringField(TEXT("$schema"), TEXT("removed")); Schema->SetBoolField(TEXT("additionalProperties"), false); A->SetObjectField(TEXT("inputSchema"), Schema);
    auto B = MakeShared<FJsonObject>(*A); B->SetStringField(TEXT("name"), TEXT("fixture_a"));
    auto Forbidden = MakeShared<FJsonObject>(*A); Forbidden->SetStringField(TEXT("name"), TEXT("fixture.execute"));
    C.ToolGroups[TEXT("read")].Append({TEXT("fixture.a"), TEXT("fixture_a"), TEXT("fixture.execute")});
    FKHUToolBridge Bridge(C, {MakeShared<FJsonValueObject>(A), MakeShared<FJsonValueObject>(B), MakeShared<FJsonValueObject>(Forbidden)});
    const auto& D = Bridge.Definitions(TEXT("T1"), 0); TestEqual(TEXT("Never overrides whitelist"), D.Num(), 2);
    const auto F1 = D[0]->AsObject()->GetObjectField(TEXT("function")), F2 = D[1]->AsObject()->GetObjectField(TEXT("function"));
    TestNotEqual(TEXT("Collisions mapped to distinct names"), F1->GetStringField(TEXT("name")), F2->GetStringField(TEXT("name")));
    TestFalse(TEXT("Unsupported schema metadata removed"), F1->GetObjectField(TEXT("parameters"))->HasField(TEXT("$schema")));
    Schema->SetStringField(TEXT("$ref"), TEXT("#/$defs/x")); FKHUToolBridge Bad(C, {MakeShared<FJsonValueObject>(A)}); TestEqual(TEXT("Unresolved ref rejected"), Bad.Definitions(TEXT("T1"), 1).Num(), 0);
    const FString Big = TEXT("HEAD") + FString::ChrN(10000, TEXT("가")[0]) + TEXT("TAIL"); const FString Short = FKHUToolBridge::Compress(Big, 1500);
    TestTrue(TEXT("Both ends retained"), Short.StartsWith(TEXT("HEAD")) && Short.EndsWith(TEXT("TAIL")) && Short.Contains(TEXT("truncated")) && Short.Len() <= 1500); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Protocol, "KHUTutor.AI.ChatProtocol", Flags)
bool FD7Protocol::RunTest(const FString& Parameters)
{
    FString Raw; FFileHelper::LoadFileToString(Raw, *(Config().FixtureDirectory / TEXT("scenario_b.json"))); TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Raw, O);
    const auto R = KHUChatProtocol::Parse(KHUMcpProtocol::WriteObject(O->GetArrayField(TEXT("responses"))[0]->AsObject().ToSharedRef()));
    TestTrue(TEXT("Null content with tool calls is valid"), R.bSuccess && R.Content.IsEmpty() && R.Calls.Num() == 1);
    TestFalse(TEXT("Malformed response rejected"), KHUChatProtocol::Parse(TEXT("{\"choices\":[]}")).bSuccess);
    FKHUChatRequest Request; Request.Model = TEXT("fixture_model"); Request.MaxTokens = 10; Request.Temperature = 0.2; Request.ReasoningEffort = TEXT("low");
    KHUMcpProtocol::ReadObject(KHUChatProtocol::Body(Request), O); TestFalse(TEXT("Reasoning omits temperature"), O->HasField(TEXT("temperature"))); TestFalse(TEXT("No tools omits tool_choice"), O->HasField(TEXT("tool_choice")));
    TestTrue(TEXT("401 diagnostic"), KHUChatProtocol::Error(401).Contains(TEXT("API 키"))); TestTrue(TEXT("402 diagnostic"), KHUChatProtocol::Error(402).Contains(TEXT("크레딧"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7ScenarioA, "KHUTutor.AI.ScenarioA", Flags)
bool FD7ScenarioA::RunTest(const FString& Parameters)
{
    auto S = RunScenario(TEXT("scenario_a")); AddCommand(new FCheck(this, S, [this, S]() { TestEqual(TEXT("No tools"), S->Executor->Names.Num(), 0); TestEqual(TEXT("One model response"), S->Loop->Receipt()->GetNumberField(TEXT("iterations")), 1.0); TestTrue(TEXT("Korean answer"), S->Text.Contains(TEXT("안녕하세요"))); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7ScenarioB, "KHUTutor.AI.ScenarioB", Flags)
bool FD7ScenarioB::RunTest(const FString& Parameters)
{
    auto S = RunScenario(TEXT("scenario_b")); AddCommand(new FCheck(this, S, [this, S]() { TestEqual(TEXT("One graph read"), S->Executor->Names.Num(), 1); TestTrue(TEXT("Graph tool"), S->Executor->Names.Num() == 1 && S->Executor->Names[0].EndsWith(TEXT("GetBlueprintGraphDigest"))); TestEqual(TEXT("Two model responses"), S->Loop->Receipt()->GetNumberField(TEXT("iterations")), 2.0); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7ScenarioC, "KHUTutor.AI.ScenarioC", Flags)
bool FD7ScenarioC::RunTest(const FString& Parameters)
{
    auto S = RunScenario(TEXT("scenario_c")); AddCommand(new FCheck(this, S, [this, S]() { TestEqual(TEXT("Two serial reads"), S->Executor->Names.Num(), 2); TestTrue(TEXT("Log before graph"), S->Executor->Names.Num() == 2 && S->Executor->Names[0].EndsWith(TEXT("GetLogDigest")) && S->Executor->Names[1].EndsWith(TEXT("GetBlueprintGraphDigest"))); TestEqual(TEXT("Three model responses"), S->Loop->Receipt()->GetNumberField(TEXT("iterations")), 3.0); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Resume, "KHUTutor.AI.PauseResume", Flags)
bool FD7Resume::RunTest(const FString& Parameters)
{
    auto C = Config(); C.Tiers[TEXT("T1")].MaxIters = 1; auto S = RunScenario(TEXT("scenario_b"), C);
    AddCommand(new FCheck(this, S, [this, S]()
    { TestTrue(TEXT("Paused after complete tool batch"), S->Loop->State() == EKHUAgentState::AskContinue); S->Done = false; S->Loop->Resume();
      AddCommand(new FCheck(this, S, [this, S]() { TestTrue(TEXT("Resumed to final answer"), S->Loop->State() == EKHUAgentState::Idle); TestEqual(TEXT("Completed tool not repeated"), S->Executor->Names.Num(), 1); })); }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Blocked, "KHUTutor.AI.BlockedAndRepeatedErrors", Flags)
bool FD7Blocked::RunTest(const FString& Parameters)
{
    auto C = Config(); FString Raw; FFileHelper::LoadFileToString(Raw, *(C.FixtureDirectory / TEXT("scenario_b.json"))); TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Raw, O);
    auto Response = O->GetArrayField(TEXT("responses"))[0]->AsObject(); const auto F = Response->GetArrayField(TEXT("choices"))[0]->AsObject()->GetObjectField(TEXT("message"))->GetArrayField(TEXT("tool_calls"))[0]->AsObject()->GetObjectField(TEXT("function"));
    F->SetStringField(TEXT("name"), TEXT("execute_python")); const FString Bad = KHUMcpProtocol::WriteObject(Response.ToSharedRef());
    auto Chat = MakeShared<FKHUMockChatClient>(TArray<FString>{Bad, Bad, Answer(TEXT("should not run"))}); auto S = MakeShared<FState>(); S->Executor = MakeShared<FExecutor>();
    S->Loop = MakeShared<FKHUAgentLoop>(C, Chat, S->Executor.ToSharedRef(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    S->Loop->Ask(TEXT("fixture"), TEXT("T1"), 1, [S](EKHUAgentState State, const FString& Text) { if (State == EKHUAgentState::Idle) { S->Done = true; S->Text = Text; } });
    AddCommand(new FCheck(this, S, [this, S]() { TestEqual(TEXT("Never dispatched"), S->Executor->Names.Num(), 0); TestTrue(TEXT("Stopped repeated denied tool"), S->Text.Contains(TEXT("두 번"))); TestEqual(TEXT("Two errors recorded"), S->Loop->Receipt()->GetArrayField(TEXT("tools")).Num(), 2); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Cancel, "KHUTutor.AI.CancelLateResponse", Flags)
bool FD7Cancel::RunTest(const FString& Parameters)
{
    auto S = RunScenario(TEXT("scenario_b")); S->Loop->Cancel(); S->Done = true;
    AddCommand(new FCheck(this, S, [this, S]() { TestEqual(TEXT("Canceled response cannot execute a tool"), S->Executor->Names.Num(), 0); TestEqual(TEXT("No stale usage"), S->Loop->Receipt()->GetArrayField(TEXT("usage")).Num(), 0); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Http, "KHUTutor.AI.HttpRetryAndCatalog", Flags)
bool FD7Http::RunTest(const FString& Parameters)
{
    auto S = MakeShared<FState>(); auto C = Config(); S->Transport = MakeShared<FTransport>();
    S->Transport->Replies.Add(Reply(200, TEXT("{\"data\":[{\"id\":\"fixture_model\",\"type\":\"llm\",\"endpoints\":[\"/v1/gateway/chat/completions/\"]}]}")));
    auto Rate = Reply(429); Rate.Headers.Add(TEXT("Retry-After"), TEXT("0")); S->Transport->Replies.Add(Rate); S->Transport->Replies.Add(Reply(200, Answer(TEXT("성공"))));
    S->Http = MakeShared<FKHUChatClient>(C, TEXT("fixture_secret_123456"), S->Transport.ToSharedRef());
    FKHUChatRequest R; R.Model = TEXT("fixture_model"); R.MaxTokens = 10; R.OutputHardLimit = 20; R.bUnlimitedDevelopment = true;
    S->Http->Send(R, [S](FKHUChatResponse Response) { S->Response = MoveTemp(Response); S->Done = true; });
    AddCommand(new FCheck(this, S, [this, S]()
    { TestTrue(TEXT("Retry succeeded"), S->Response.bSuccess); TestEqual(TEXT("Two paid attempts"), S->Response.Attempts, 2); TestEqual(TEXT("GET catalog plus two POSTs"), S->Transport->Requests.Num(), 3); TestEqual(TEXT("First request GET"), S->Transport->Requests[0].Verb, FString(TEXT("GET"))); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Live, "KHUTutor.AI.Live.OpusMcp", Flags)
bool FD7Live::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorD7Live"))) { AddInfo(TEXT("Paid test not requested; use -D7Live only with authorization")); return true; }
    auto S = MakeShared<FState>(); S->Deadline = FPlatformTime::Seconds() + 180;
    TestTrue(TEXT("Start actual plugin client"), IKHUTutorModule::Get().AskTutor(TEXT("반드시 Ping 도구를 한 번 호출하세요. message는 d7-opus-live 입니다. 받은 PONG의 engine, project, project_file을 한국어로 짧게 보고하세요. 다른 도구나 에디터 조작은 하지 마세요."), TEXT(""), true, TEXT("D7")));
    S->Loop = IKHUTutorModule::Get().GetAgentLoop(); if (!S->Loop) { return false; }
    class FLiveCheck final : public IAutomationLatentCommand
    {
    public:
        FLiveCheck(FAutomationTestBase* InTest, TSharedRef<FState> InState) : Test(InTest), State(InState) {}
        virtual bool Update() override
        {
            if (State->Loop->State() != EKHUAgentState::Idle && FPlatformTime::Seconds() < State->Deadline) { return false; }
            const auto O = State->Loop->Receipt(); const auto Tools = O->GetArrayField(TEXT("tools")); const auto Usage = O->GetArrayField(TEXT("usage"));
            Test->TestTrue(TEXT("Actual Opus final answer and successful MCP Ping"), State->Loop->State() == EKHUAgentState::Idle && Tools.Num() == 1 && Tools[0]->AsObject()->GetBoolField(TEXT("success")) && Tools[0]->AsObject()->GetStringField(TEXT("result")).Contains(TEXT("PONG: d7-opus-live")) && O->GetStringField(TEXT("text")).Contains(TEXT("TutorSandbox")));
            Test->TestTrue(TEXT("At least two actual model responses"), Usage.Num() >= 2 && !Usage[0]->AsObject()->GetBoolField(TEXT("mock")));
            const FString Path = FPaths::ProjectSavedDir() / TEXT("KHUTutor/D7/live_result.json");
            Test->TestTrue(TEXT("Live receipt saved"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(O), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            if (State->Loop->State() != EKHUAgentState::Idle) { State->Loop->Cancel(); } return true;
        }
    private: FAutomationTestBase* Test; TSharedRef<FState> State;
    };
    AddCommand(new FLiveCheck(this, S)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7HttpMatrix, "KHUTutor.AI.HttpErrorMatrix", Flags)
bool FD7HttpMatrix::RunTest(const FString& Parameters)
{
    auto S = MakeShared<FState>(); const auto C = Config(); S->Transport = MakeShared<FTransport>();
    S->Transport->Replies.Add(Reply(200, TEXT("{\"data\":[{\"id\":\"fixture_model\",\"type\":\"llm\",\"endpoints\":[\"/v1/gateway/chat/completions/\"]}]}")));
    S->Transport->Replies.Add(Reply(413)); S->Transport->Replies.Add(Reply(200, Answer(TEXT(""))));
    S->Transport->Replies.Add(Reply(500)); S->Transport->Replies.Add(Reply(200, Answer(TEXT("성공"))));
    S->Http = MakeShared<FKHUChatClient>(C, TEXT("fixture_secret_123456"), S->Transport.ToSharedRef());
    FKHUChatRequest R; R.Model = TEXT("fixture_model"); R.MaxTokens = 10; R.OutputHardLimit = 20; R.bUnlimitedDevelopment = true;
    auto M = MakeShared<FJsonObject>(); M->SetStringField(TEXT("role"), TEXT("system")); M->SetStringField(TEXT("content"), TEXT("PRESERVE_SYSTEM_RULES")); R.Messages.Add(MakeShared<FJsonValueObject>(M));
    auto Original = MakeShared<FJsonObject>(); Original->SetStringField(TEXT("role"), TEXT("user")); Original->SetStringField(TEXT("content"), TEXT("ORIGINAL_TASK")); R.Messages.Add(MakeShared<FJsonValueObject>(Original));
    auto Old = MakeShared<FJsonObject>(); Old->SetStringField(TEXT("role"), TEXT("assistant")); Old->SetStringField(TEXT("content"), FString::ChrN(8000, 'x')); R.Messages.Add(MakeShared<FJsonValueObject>(Old));
    auto Current = MakeShared<FJsonObject>(); Current->SetStringField(TEXT("role"), TEXT("user")); Current->SetStringField(TEXT("content"), TEXT("한글 문맥 테스트입니다")); R.Messages.Add(MakeShared<FJsonValueObject>(Current));
    S->Http->Send(R, [S](FKHUChatResponse Response) { S->Response = MoveTemp(Response); S->Done = true; });
    AddCommand(new FCheck(this, S, [this, S]()
    {
        TestTrue(TEXT("413 + empty + transient retries succeed"), S->Response.bSuccess); TestEqual(TEXT("Retries bounded"), S->Response.Attempts, 4);
        if (S->Transport->Requests.Num() == 5)
        {
            TSharedPtr<FJsonObject> First, Second, Expanded; KHUMcpProtocol::ReadObject(S->Transport->Requests[1].Body, First);
            KHUMcpProtocol::ReadObject(S->Transport->Requests[2].Body, Second); KHUMcpProtocol::ReadObject(S->Transport->Requests[3].Body, Expanded);
            TestTrue(TEXT("Only old completed turns reduced after 413"), S->Transport->Requests[2].Body.Len() < S->Transport->Requests[1].Body.Len());
            TestTrue(TEXT("System and current question preserved"), S->Transport->Requests[2].Body.Contains(TEXT("PRESERVE_SYSTEM_RULES")) && S->Transport->Requests[2].Body.Contains(TEXT("한글 문맥 테스트입니다")));
            TestEqual(TEXT("Original shared message unchanged"), First->GetArrayField(TEXT("messages"))[0]->AsObject()->GetStringField(TEXT("content")), FString(TEXT("PRESERVE_SYSTEM_RULES")));
            TestEqual(TEXT("Empty response expands only within cap"), Expanded->GetNumberField(TEXT("max_tokens")), 20.0);
        }
    })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7HttpGuard, "KHUTutor.AI.HttpCreditGuardAndCancel", Flags)
bool FD7HttpGuard::RunTest(const FString& Parameters)
{
    auto S = MakeShared<FState>(); auto C = Config(); S->Transport = MakeShared<FTransport>();
    S->Transport->Replies.Add(Reply(200, TEXT("{\"data\":[{\"id\":\"fixture_model\",\"type\":\"llm\",\"endpoints\":[\"/v1/gateway/chat/completions/\"]}]}")));
    S->Http = MakeShared<FKHUChatClient>(C, TEXT("fixture_secret_123456"), S->Transport.ToSharedRef());
    FKHUChatRequest R; R.Model = TEXT("fixture_model"); R.MaxTokens = 10; R.OutputHardLimit = 20; R.RemainingCredits = 9; R.AttemptCreditReserve = 10;
    S->Http->Send(R, [S](FKHUChatResponse Response) { S->Response = MoveTemp(Response); S->Done = true; });
    AddCommand(new FCheck(this, S, [this, S, C, R]() mutable
    {
        TestTrue(TEXT("Credit guard returns a pause"), S->Response.bBudget); TestEqual(TEXT("Zero billed attempts"), S->Response.Attempts, 0);
        TestEqual(TEXT("Only free model discovery"), S->Transport->Requests.Num(), 1);
        auto T = MakeShared<FTransport>(); T->Replies.Add(Reply(200, TEXT("{\"data\":[]}")));
        auto H = MakeShared<FKHUChatClient>(C, TEXT("fixture_secret_123456"), T); H->Send(R, [S](FKHUChatResponse) { S->CanceledCallback = true; }); H->Cancel();
        auto Next = MakeShared<FState>(); Next->Done = true;
        AddCommand(new FCheck(this, Next, [this, S, H, T]() { TestFalse(TEXT("Canceled HTTP ignores late completion"), S->CanceledCallback); }));
    })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7Direct, "KHUTutor.AI.DirectExecutor", Flags)
bool FD7Direct::RunTest(const FString& Parameters)
{
    auto S = MakeShared<FState>(); auto Executor = MakeShared<FKHUToolExecutorDirect>(); auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("message"), TEXT("d7-direct"));
    Executor->Execute(FKHUMcpClient::PingToolName(), Args, [S](FKHUMcpResult) { S->CanceledCallback = true; }); Executor->Cancel();
    Executor->Execute(FKHUMcpClient::PingToolName(), Args, [S](FKHUMcpResult R) { S->ToolResult = MoveTemp(R); S->Done = true; });
    AddCommand(new FCheck(this, S, [this, S, Executor]() { TestTrue(TEXT("Owned static Ping"), S->ToolResult.bSuccess && S->ToolResult.Text.Contains(TEXT("PONG: d7-direct"))); TestFalse(TEXT("Canceled direct action never executes"), S->CanceledCallback); })); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD7OwnedRepeat, "KHUTutor.AI.CanonicalRepeatedToolError", Flags)
bool FD7OwnedRepeat::RunTest(const FString& Parameters)
{
    auto C = Config(); FString Raw; FFileHelper::LoadFileToString(Raw, *(C.FixtureDirectory / TEXT("scenario_b.json"))); TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Raw, O);
    const auto Response = O->GetArrayField(TEXT("responses"))[0]->AsObject(); const FString First = KHUMcpProtocol::WriteObject(Response.ToSharedRef());
    const auto Call = Response->GetArrayField(TEXT("choices"))[0]->AsObject()->GetObjectField(TEXT("message"))->GetArrayField(TEXT("tool_calls"))[0]->AsObject();
    Call->SetStringField(TEXT("id"), TEXT("different_call_id")); Call->GetObjectField(TEXT("function"))->SetStringField(TEXT("arguments"), TEXT("{\"hops\":2,\"maxNodes\":20,\"focus\":\"errors\",\"graphName\":\"\",\"assetPath\":\"/Game/KHUTest/BP_Door_Broken\"}"));
    auto Chat = MakeShared<FKHUMockChatClient>(TArray<FString>{First, KHUMcpProtocol::WriteObject(Response.ToSharedRef())}); auto S = MakeShared<FState>(); S->Executor = MakeShared<FExecutor>(); S->Executor->Fail = true;
    S->Loop = MakeShared<FKHUAgentLoop>(C, Chat, S->Executor.ToSharedRef(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    S->Loop->Ask(TEXT("fixture"), TEXT("T1"), 1, [S](EKHUAgentState State, const FString& Text) { if (State == EKHUAgentState::Idle) { S->Done = true; S->Text = Text; } });
    AddCommand(new FCheck(this, S, [this, S]() { TestEqual(TEXT("Two actual executor failures"), S->Executor->Names.Num(), 2); TestTrue(TEXT("Key order does not bypass repeat detection"), S->Text.Contains(TEXT("두 번"))); })); return true;
}
#endif

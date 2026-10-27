#include "Mcp/KHUMcpClient.h"
#include "KHUTutorModule.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceNull.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedRef<FJsonObject> Object() { return MakeShared<FJsonObject>(); }
    FString Rpc(int32 Id, const TSharedRef<FJsonObject>& Result)
    {
        auto Packet = Object(); Packet->SetStringField(TEXT("jsonrpc"), TEXT("2.0")); Packet->SetNumberField(TEXT("id"), Id);
        Packet->SetObjectField(TEXT("result"), Result); return KHUMcpProtocol::WriteObject(Packet);
    }
    TSharedRef<FJsonObject> TextResult(const FString& Text)
    {
        auto Content = Object(); Content->SetStringField(TEXT("type"), TEXT("text")); Content->SetStringField(TEXT("text"), Text);
        auto Result = Object(); Result->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Content)}); return Result;
    }
    FKHUMcpOptions TestOptions()
    {
        FKHUMcpOptions Options; Options.ExpectedProject = TEXT("C:/KHU/TutorSandbox.uproject");
        Options.AllowedTools.Add(FKHUMcpClient::PingToolName());
        Options.NeverPatterns.Add(TEXT("*Execute*")); return Options;
    }

    class FManualTransport final : public IKHUMcpTransport
    {
    public:
        TArray<FCompletion> Callbacks;
        TArray<FKHUMcpHttpRequest> Requests;
        TSet<int32> Closed;
        int32 InFlight = 0, MaxInFlight = 0;
        virtual FCancel Send(const FKHUMcpHttpRequest& Request, FCompletion Completion) override
        {
            const int32 Index = Requests.Add(Request); Callbacks.Add(MoveTemp(Completion));
            ++InFlight; MaxInFlight = FMath::Max(MaxInFlight, InFlight);
            return [this, Index]() { Close(Index); };
        }
        void Close(int32 Index) { if (!Closed.Contains(Index)) { Closed.Add(Index); --InFlight; } }
        void Reply(int32 Index, FKHUMcpHttpResponse Response = {})
        {
            Close(Index);
            // Keep a copy to deliberately simulate a late transport completion after cancellation.
            auto Callback = Callbacks[Index]; Callback(MoveTemp(Response));
        }
    };

    class FFakeEditor final : public IKHUMcpTransport
    {
    public:
        TArray<FKHUMcpHttpRequest> Requests;
        int32 Initializes = 0, Pings = 0, ReadAttempts = 0;
        bool bWithContext = false, bContextError = false, bExpireReadOnce = false, bWrongAfterReadExpiry = false;
        bool bEager = false, bPaged = false, bRepeatedCursor = false;
        bool bExpireOnce = false, bExpireAlways = false, bMissingSession = false, bWrongProject = false;
        bool bToolError = false, bBadRequest = false, bTimeout = false, bUnsupportedVersion = false;
        virtual FCancel Send(const FKHUMcpHttpRequest& Request, FCompletion Completion) override
        {
            Requests.Add(Request);
            TSharedPtr<FJsonObject> Input; KHUMcpProtocol::ReadObject(Request.Body, Input);
            const FString Method = Input->GetStringField(TEXT("method"));
            const auto Params = Input->GetObjectField(TEXT("params"));
            const int32 Id = Method == TEXT("notifications/initialized") ? 0 : static_cast<int32>(Input->GetNumberField(TEXT("id")));
            FKHUMcpHttpResponse Response; Response.Status = 200; Response.Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
            auto Result = Object();
            if (Method == TEXT("initialize"))
            {
                ++Initializes;
                Response.Headers.Add(TEXT("mcp-session-id"), FString::Printf(TEXT("session-%d"), Initializes));
                Result->SetStringField(TEXT("protocolVersion"), bUnsupportedVersion ? TEXT("2099-01-01") : TEXT("2025-06-18"));
            }
            else if (Method == TEXT("notifications/initialized")) { Response.Status = 202; Completion(MoveTemp(Response)); return []() {}; }
            else if (Method == TEXT("tools/list"))
            {
                const bool bSecondPage = Params->HasField(TEXT("cursor"));
                TArray<TSharedPtr<FJsonValue>> Tools;
                if (bEager) { Tools.Add(MakeShared<FJsonValueObject>(PingSchema())); if (bWithContext) { Tools.Add(MakeShared<FJsonValueObject>(ContextSchema())); } }
                else
                {
                    for (const FString Name : {FString(TEXT("list_toolsets")), FString(TEXT("describe_toolset")), FString(TEXT("call_tool"))})
                    {
                        if (bPaged && ((bSecondPage && Name == TEXT("list_toolsets")) || (!bSecondPage && Name != TEXT("list_toolsets")))) { continue; }
                        auto Tool = Object(); Tool->SetStringField(TEXT("name"), Name); Tools.Add(MakeShared<FJsonValueObject>(Tool));
                    }
                }
                Result->SetArrayField(TEXT("tools"), Tools);
                if (bRepeatedCursor || (bPaged && !bSecondPage)) { Result->SetStringField(TEXT("nextCursor"), TEXT("page2")); }
            }
            else
            {
                const FString Name = Params->GetStringField(TEXT("name"));
                if (Name == TEXT("list_toolsets")) { Result = TextResult(TEXT("KHUTutorTools.KHUTutorReadToolset")); }
                else if (Name == TEXT("describe_toolset"))
                {
                    auto Description = Object();
                    TArray<TSharedPtr<FJsonValue>> Described = {MakeShared<FJsonValueObject>(PingSchema())};
                    if (bWithContext) { Described.Add(MakeShared<FJsonValueObject>(ContextSchema())); }
                    Description->SetArrayField(TEXT("tools"), Described);
                    Result = TextResult(KHUMcpProtocol::WriteObject(Description));
                }
                else
                {
                    FString Function = Name;
                    if (Name == TEXT("call_tool")) { Function = Params->GetObjectField(TEXT("arguments"))->GetStringField(TEXT("tool_name")); }
                    if (Function == TEXT("GetTutorContext") || Function.EndsWith(TEXT(".GetTutorContext")))
                    {
                        ++ReadAttempts;
                        if (bExpireReadOnce && ReadAttempts == 1)
                        {
                            Response.Status = 404; Response.Body = TEXT("Unknown session id 'expired'; client should reinitialize");
                            bWrongProject = bWrongAfterReadExpiry; Completion(MoveTemp(Response)); return []() {};
                        }
                        auto Wrapped = Object(); Wrapped->SetStringField(TEXT("returnValue"), bContextError ? TEXT("ERROR: fake read error") : TEXT("ENGINE fake | PROJECT TutorSandbox"));
                        Response.Body = Rpc(Id, TextResult(KHUMcpProtocol::WriteObject(Wrapped))); Completion(MoveTemp(Response)); return []() {};
                    }
                    ++Pings;
                    if (bExpireAlways || (bExpireOnce && Pings == 1))
                    { Response.Status = bMissingSession ? 400 : 404; Response.Body = bMissingSession ? TEXT("Missing required Mcp-Session-Id header") : TEXT("Unknown session id 'expired'; client should reinitialize"); Completion(MoveTemp(Response)); return []() {}; }
                    if (bBadRequest) { Response.Status = 400; Response.Body = TEXT("Invalid parameters"); Completion(MoveTemp(Response)); return []() {}; }
                    if (bTimeout) { Response.Status = 0; Response.Error = TEXT("Timeout"); Completion(MoveTemp(Response)); return []() {}; }
                    if (bToolError) { Result->SetBoolField(TEXT("isError"), true); }
                    else
                    {
                        auto Wrapped = Object(); Wrapped->SetStringField(TEXT("returnValue"),
                            FString(TEXT("PONG: \ud55c\uae00 | engine 5.8.3 | project TutorSandbox | project_file ")) +
                            (bWrongProject ? TEXT("C:/Other/Other.uproject") : TEXT("C:/KHU/TutorSandbox.uproject")));
                        Result = TextResult(KHUMcpProtocol::WriteObject(Wrapped));
                    }
                }
            }
            Response.Body = Rpc(Id, Result); Completion(MoveTemp(Response)); return []() {};
        }
        static TSharedRef<FJsonObject> PingSchema()
        {
            auto Schema = Object(); Schema->SetStringField(TEXT("name"), FKHUMcpClient::PingToolName());
            TSharedPtr<FJsonObject> Input;
            KHUMcpProtocol::ReadObject(TEXT("{\"type\":\"object\",\"properties\":{\"message\":{\"type\":\"string\"}},\"required\":[\"message\"]}"), Input);
            Schema->SetObjectField(TEXT("inputSchema"), Input); return Schema;
        }
        static TSharedRef<FJsonObject> ContextSchema()
        {
            TSharedPtr<FJsonObject> Schema;
            KHUMcpProtocol::ReadObject(TEXT("{\"name\":\"KHUTutorTools.KHUTutorReadToolset.GetTutorContext\",\"inputSchema\":{\"type\":\"object\"},\"outputSchema\":{\"type\":\"object\",\"properties\":{\"returnValue\":{\"type\":\"string\"}}}}"), Schema);
            return Schema.ToSharedRef();
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUJsonRpcTest, "KHUTutor.Mcp.Protocol.JsonRpc", Flags)
bool FKHUJsonRpcTest::RunTest(const FString& Parameters)
{
    auto Result = Object(); Result->SetStringField(TEXT("value"), TEXT("\ud55c\uae00"));
    auto Parsed = KHUMcpProtocol::ParseResponse(Rpc(7, Result), TEXT("application/json;charset=utf-8"), 7);
    TestTrue(TEXT("Correct ID and UTF-8 text"), Parsed.bSuccess && Parsed.Object->GetStringField(TEXT("value")) == TEXT("\ud55c\uae00"));
    TestFalse(TEXT("Wrong numeric ID"), KHUMcpProtocol::ParseResponse(Rpc(8, Result), TEXT("application/json"), 7).bSuccess);
    TestFalse(TEXT("String ID is not coerced"), KHUMcpProtocol::ParseResponse(TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"7\",\"result\":{}}"), TEXT("application/json"), 7).bSuccess);
    TestFalse(TEXT("Boolean ID is not coerced"), KHUMcpProtocol::ParseResponse(TEXT("{\"jsonrpc\":\"2.0\",\"id\":true,\"result\":{}}"), TEXT("application/json"), 1).bSuccess);
    TestFalse(TEXT("Malformed JSON"), KHUMcpProtocol::ParseResponse(TEXT("{"), TEXT("application/json"), 7).bSuccess);
    auto Error = KHUMcpProtocol::ParseResponse(TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"error\":{\"code\":-32602,\"message\":\"Bad parameters\"}}"), TEXT("application/json"), 7);
    TestFalse(TEXT("RPC error"), Error.bSuccess); TestEqual(TEXT("RPC code preserved"), Error.RpcErrorCode, -32602);
    TestFalse(TEXT("Unexpected content type"), KHUMcpProtocol::ParseResponse(Rpc(7, Result), TEXT("text/html"), 7).bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUSseTest, "KHUTutor.Mcp.Protocol.Sse", Flags)
bool FKHUSseTest::RunTest(const FString& Parameters)
{
    const FString Stream = TEXT(": heartbeat\r\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\r\n\r\n")
        TEXT("data: {\"jsonrpc\":\"2.0\",\"id\":7,\r\ndata: \"result\":{\"value\":\"\ud55c\uae00\"}}\r\n\r\n");
    auto Parsed = KHUMcpProtocol::ParseResponse(Stream, TEXT("text/event-stream"), 7);
    TestTrue(TEXT("Multiline SSE and notification"), Parsed.bSuccess && Parsed.Object->GetStringField(TEXT("value")) == TEXT("\ud55c\uae00"));
    TestFalse(TEXT("No fallback to notification"), KHUMcpProtocol::ParseResponse(Stream, TEXT("text/event-stream"), 8).bSuccess);
    TestTrue(TEXT("EOF event without blank line"), KHUMcpProtocol::ParseResponse(TEXT("data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}"), TEXT("text/event-stream"), 1).bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUToolTextTest, "KHUTutor.Mcp.Protocol.ToolIdentity", Flags)
bool FKHUToolTextTest::RunTest(const FString& Parameters)
{
    FString Identity;
    const FString Text = TEXT("{\"returnValue\":\"PONG: hi | project_file C:/KHU/TutorSandbox.uproject\"}");
    auto Result = KHUMcpProtocol::ToolResult(TextResult(Text));
    TestTrue(TEXT("Observed returnValue wrapper"), Result.bSuccess && KHUMcpProtocol::VerifyPingProject(Result.Text, TestOptions().ExpectedProject, Identity));
    TestFalse(TEXT("Wrong project rejected"), KHUMcpProtocol::VerifyPingProject(Text, TEXT("C:/Other/Other.uproject"), Identity));
    auto Error = TextResult(TEXT("bad")); Error->SetBoolField(TEXT("isError"), true);
    TestFalse(TEXT("Tool error not success"), KHUMcpProtocol::ToolResult(Error).bSuccess);
    auto Structured = Object(); auto Inner = Object(); Inner->SetStringField(TEXT("x"), TEXT("y")); Structured->SetObjectField(TEXT("structuredContent"), Inner);
    TestTrue(TEXT("Structured result fallback"), KHUMcpProtocol::ToolResult(Structured).bSuccess);
    TestFalse(TEXT("Missing tool content"), KHUMcpProtocol::ToolResult(Object()).bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUUrlTest, "KHUTutor.Mcp.Protocol.EndpointSafety", Flags)
bool FKHUUrlTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("8001 permitted"), KHUMcpProtocol::IsSafeLocalUrl(TEXT("http://127.0.0.1:8001/mcp")));
    for (const FString Url : {FString(TEXT("http://127.0.0.1:8000/mcp")), FString(TEXT("http://127.0.0.1:8765/mcp")),
        FString(TEXT("http://example.com:8001/mcp")), FString(TEXT("http://user@127.0.0.1:8001/mcp")),
        FString(TEXT("http://127.0.0.1:8001/mcp?x=1")), FString(TEXT("http://127.0.0.1:0/mcp")), FString(TEXT("http://127.0.0.1:99999/mcp"))})
    { TestFalse(*Url, KHUMcpProtocol::IsSafeLocalUrl(Url)); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUQueueTest, "KHUTutor.Mcp.Queue.FifoAndCancellation", Flags)
bool FKHUQueueTest::RunTest(const FString& Parameters)
{
    auto Transport = MakeShared<FManualTransport>(); auto Queue = MakeShared<FKHUMcpSerialQueue>(Transport);
    TArray<int32> Order;
    Queue->Enqueue(1, {}, [&Order, Queue](FKHUMcpHttpResponse)
    { Order.Add(1); Queue->Enqueue(1, {}, [&Order](FKHUMcpHttpResponse) { Order.Add(3); }); });
    Queue->Enqueue(2, {}, [&Order](FKHUMcpHttpResponse) { Order.Add(2); });
    TestEqual(TEXT("Only one request initially"), Transport->Requests.Num(), 1);
    Transport->Reply(0); Transport->Reply(1); Transport->Reply(2);
    TestTrue(TEXT("Reentrant enqueue preserves FIFO"), Order == TArray<int32>({1, 2, 3}));
    int32 Cancelled = 0, Other = 0;
    Queue->Enqueue(1, {}, [&Cancelled](FKHUMcpHttpResponse R) { if (!R.Error.IsEmpty()) { ++Cancelled; } });
    Queue->Enqueue(2, {}, [&Other](FKHUMcpHttpResponse) { ++Other; });
    Queue->Enqueue(1, {}, [&Cancelled](FKHUMcpHttpResponse R) { if (!R.Error.IsEmpty()) { ++Cancelled; } });
    Queue->CancelOwner(1); Transport->Reply(3); Transport->Reply(4);
    TestEqual(TEXT("Cancelled owner completed once per request"), Cancelled, 2);
    TestEqual(TEXT("Other owner unaffected and late response ignored"), Other, 1);
    TestEqual(TEXT("Global maximum in-flight"), Transport->MaxInFlight, 1);
    Queue->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUClientDiscoveryTest, "KHUTutor.Mcp.Client.DiscoveryAndHeaders", Flags)
bool FKHUClientDiscoveryTest::RunTest(const FString& Parameters)
{
    for (bool bEager : {false, true})
    {
        auto Editor = MakeShared<FFakeEditor>(); Editor->bEager = bEager; Editor->bPaged = !bEager;
        auto Queue = MakeShared<FKHUMcpSerialQueue>(Editor); auto Client = MakeShared<FKHUMcpClient>(Queue, TestOptions());
        int32 Completed = 0;
        Client->HealthCheck([this, &Completed](FKHUMcpResult Result) { ++Completed; TestTrue(TEXT("Health and exact identity"), Result.bSuccess && Result.Text.StartsWith(TEXT("PONG:"))); });
        Client->CallTool(FKHUMcpClient::PingToolName(), TEXT("{\"message\":\"\ud55c\uae00\"}"), [this, &Completed](FKHUMcpResult R) { ++Completed; TestTrue(TEXT("Queued Ping"), R.bSuccess); });
        TestEqual(TEXT("Both operations finish exactly once"), Completed, 2);
        TestEqual(TEXT("Session reused"), Editor->Initializes, 1);
        TestFalse(TEXT("Initialized has no ID"), Editor->Requests[1].Body.Contains(TEXT("\"id\"")));
        TestTrue(TEXT("Session header carried"), Editor->Requests[1].Headers.Contains(TEXT("Mcp-Session-Id")));
        TestEqual(TEXT("Negotiated protocol header"), Editor->Requests[1].Headers[TEXT("MCP-Protocol-Version")], FString(TEXT("2025-06-18")));
        TestEqual(TEXT("Control timeout"), Editor->Requests[0].TimeoutSeconds, 10.0f);
        TestEqual(TEXT("Call timeout"), Editor->Requests.Last().TimeoutSeconds, 120.0f);
        if (!bEager) { TestTrue(TEXT("Search-mode dispatch confirmed"), Editor->Requests.Last().Body.Contains(TEXT("\"tool_name\": \"Ping\""))); }
        Client->Shutdown(); Queue->Shutdown();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUClientSafetyTest, "KHUTutor.Mcp.Client.WhitelistAndSchema", Flags)
bool FKHUClientSafetyTest::RunTest(const FString& Parameters)
{
    auto Editor = MakeShared<FFakeEditor>(); auto Queue = MakeShared<FKHUMcpSerialQueue>(Editor);
    auto Options = TestOptions(); Options.AllowedTools.Add(TEXT("Other.ExecutePython"));
    auto Client = MakeShared<FKHUMcpClient>(Queue, Options);
    bool bRejected = false;
    Client->CallTool(TEXT("Other.ExecutePython"), TEXT("{}"), [&bRejected](FKHUMcpResult R) { bRejected = !R.bSuccess; });
    TestTrue(TEXT("Never pattern wins over read permission"), bRejected); TestEqual(TEXT("Rejected before transport"), Editor->Requests.Num(), 0);
    Client->CallTool(FKHUMcpClient::PingToolName(), TEXT("{\"Message\":\"wrong case\"}"), [this](FKHUMcpResult R) { TestFalse(TEXT("Actual lowercase schema required"), R.bSuccess); });
    Client->CallTool(FKHUMcpClient::PingToolName(), TEXT("{\"message\":5}"), [this](FKHUMcpResult R) { TestFalse(TEXT("Argument type required"), R.bSuccess); });
    Client->CallTool(FKHUMcpClient::PingToolName(), TEXT("{}"), [this](FKHUMcpResult R) { TestFalse(TEXT("Required argument missing"), R.bSuccess); });
    Client->CallTool(FKHUMcpClient::PingToolName(), TEXT("{\"message\":\"hi\",\"extra\":\"bad\"}"), [this](FKHUMcpResult R) { TestFalse(TEXT("Unknown argument rejected"), R.bSuccess); });
    TestEqual(TEXT("Bad schema causes no Ping execution"), Editor->Pings, 0);
    Editor->bWrongProject = true;
    Client->HealthCheck([this](FKHUMcpResult R) { TestFalse(TEXT("Wrong editor rejected"), R.bSuccess); });
    Client->Shutdown(); Queue->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHURecoveryTest, "KHUTutor.Mcp.Client.SessionRecovery", Flags)
bool FKHURecoveryTest::RunTest(const FString& Parameters)
{
    for (int32 Mode = 0; Mode < 4; ++Mode)
    {
        const bool bAlways = Mode % 2 == 1;
        auto Editor = MakeShared<FFakeEditor>(); Editor->bExpireOnce = !bAlways; Editor->bExpireAlways = bAlways;
        Editor->bMissingSession = Mode >= 2;
        auto Queue = MakeShared<FKHUMcpSerialQueue>(Editor); auto Client = MakeShared<FKHUMcpClient>(Queue, TestOptions());
        int32 Completed = 0;
        Client->HealthCheck([this, &Completed, bAlways](FKHUMcpResult R) { ++Completed; TestEqual(TEXT("One recovery success or explicit failure"), R.bSuccess, !bAlways); });
        TestEqual(TEXT("Exactly one reinitialize"), Editor->Initializes, 2); TestEqual(TEXT("Exactly one replay"), Editor->Pings, 2);
        TestEqual(TEXT("Completion exactly once"), Completed, 1); Client->Shutdown(); Queue->Shutdown();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUFailureTest, "KHUTutor.Mcp.Client.NoUnsafeReplay", Flags)
bool FKHUFailureTest::RunTest(const FString& Parameters)
{
    for (int32 Mode = 0; Mode < 5; ++Mode)
    {
        auto Editor = MakeShared<FFakeEditor>();
        Editor->bBadRequest = Mode == 0; Editor->bTimeout = Mode == 1; Editor->bToolError = Mode == 2;
        Editor->bRepeatedCursor = Mode == 3; Editor->bUnsupportedVersion = Mode == 4;
        auto Queue = MakeShared<FKHUMcpSerialQueue>(Editor); auto Client = MakeShared<FKHUMcpClient>(Queue, TestOptions());
        int32 Completed = 0;
        Client->HealthCheck([this, &Completed](FKHUMcpResult R) { ++Completed; TestFalse(TEXT("Failure is explicit"), R.bSuccess); });
        TestEqual(TEXT("No automatic replay for other failures"), Editor->Initializes, 1);
        TestEqual(TEXT("Completion once even for failure"), Completed, 1); Client->Shutdown(); Queue->Shutdown();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUClientShutdownTest, "KHUTutor.Mcp.Client.Shutdown", Flags)
bool FKHUClientShutdownTest::RunTest(const FString& Parameters)
{
    auto Transport = MakeShared<FManualTransport>(); auto Queue = MakeShared<FKHUMcpSerialQueue>(Transport);
    auto Client = MakeShared<FKHUMcpClient>(Queue, TestOptions()); int32 Completed = 0;
    Client->Initialize([&Completed](FKHUMcpResult) { ++Completed; }); Client->ListTools([&Completed](FKHUMcpResult) { ++Completed; });
    TestEqual(TEXT("Only initial request started"), Transport->Requests.Num(), 1);
    Client->Shutdown(); Transport->Reply(0); TestEqual(TEXT("Both operations cancelled once; late response ignored"), Completed, 2);
    Queue->Shutdown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUD3ProjectGateTest, "KHUTutor.Mcp.Client.ReadProjectGateAndStringReturn", Flags)
bool FKHUD3ProjectGateTest::RunTest(const FString& Parameters)
{
    const FString Name = TEXT("KHUTutorTools.KHUTutorReadToolset.GetTutorContext");
    for (int32 Mode = 0; Mode < 5; ++Mode)
    {
        auto Editor = MakeShared<FFakeEditor>(); Editor->bWithContext = true;
        Editor->bWrongProject = Mode == 1; Editor->bContextError = Mode == 2;
        Editor->bExpireReadOnce = Mode >= 3; Editor->bWrongAfterReadExpiry = Mode == 4;
        auto O = TestOptions(); O.AllowedTools.Add(Name);
        auto Queue = MakeShared<FKHUMcpSerialQueue>(Editor); auto Client = MakeShared<FKHUMcpClient>(Queue, O);
        int32 Completed = 0;
        Client->CallTool(Name, TEXT("{}"), [this, &Completed, Mode](FKHUMcpResult R)
        {
            ++Completed; TestEqual(TEXT("Project gate and schema string return"), R.bSuccess, Mode == 0 || Mode == 3);
            if (R.bSuccess) { TestTrue(TEXT("String unwrapped using output schema"), R.Text.StartsWith(TEXT("ENGINE fake"))); }
            if (Mode == 2) { TestTrue(TEXT("ERROR prefix becomes explicit failure"), R.Error.StartsWith(TEXT("ERROR:"))); }
        });
        TestEqual(TEXT("Completion once"), Completed, 1);
        TestEqual(TEXT("Ping before reading, and again after recovery"), Editor->Pings, Mode >= 3 ? 2 : 1);
        TestEqual(TEXT("No read on wrong initial project; no replay on changed project"), Editor->ReadAttempts, Mode == 1 ? 0 : (Mode == 3 ? 2 : 1));
        const int32 BeforeInvalid = Editor->ReadAttempts;
        Client->CallTool(Name, TEXT("{\"unexpected\":true}"), [this](FKHUMcpResult R)
        { TestFalse(TEXT("No-argument schema rejects extra arguments"), R.bSuccess); });
        TestEqual(TEXT("Invalid arguments never dispatched"), Editor->ReadAttempts, BeforeInvalid);
        Client->Shutdown(); Queue->Shutdown();
    }
    return true;
}

namespace
{
    struct FLiveState { bool bDone = false; FKHUMcpResult Result; double Deadline = FPlatformTime::Seconds() + 30; };
    class FLiveMcpCheck final : public IAutomationLatentCommand
    {
    public:
        FLiveMcpCheck(FAutomationTestBase* InTest, TSharedRef<FLiveState> InState) : Test(InTest), State(InState) {}
        virtual bool Update() override
        {
            if (!State->bDone && FPlatformTime::Seconds() < State->Deadline) { return false; }
            if (!State->bDone) { Test->AddError(TEXT("Live MCP did not complete within 30 seconds")); return true; }
            Test->TestTrue(TEXT("Live editor health check"), State->Result.bSuccess);
            if (!State->Result.bSuccess) { Test->AddError(State->Result.Error); }
            else
            {
                for (const FString Name : {FString(TEXT("D2_list.json")), FString(TEXT("D2_call.json"))})
                {
                    FString Text; TSharedPtr<FJsonObject> ConsoleReceipt;
                    const bool bRead = FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("KHUTutor") / Name))
                        && KHUMcpProtocol::ReadObject(Text, ConsoleReceipt);
                    bool bSuccess = false;
                    Test->TestTrue(*FString::Printf(TEXT("Console receipt %s succeeded"), *Name),
                        bRead && ConsoleReceipt->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess);
                }
                auto Receipt = Object(); Receipt->SetBoolField(TEXT("success"), true); Receipt->SetStringField(TEXT("identity"), State->Result.Text);
                const FString Directory = FPaths::ProjectSavedDir() / TEXT("KHUTutor"); IFileManager::Get().MakeDirectory(*Directory, true);
                FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Receipt), *(Directory / TEXT("D2_automation_live.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            }
            return true;
        }
    private:
        FAutomationTestBase* Test;
        TSharedRef<FLiveState> State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHULiveTest, "KHUTutor.Mcp.Live.EditorRoundTrip", Flags)
bool FKHULiveTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorMcpSmoke")))
    { AddInfo(TEXT("Live MCP test is opt-in; run scripts/test.ps1 -McpSmoke.")); return true; }
    // Remove only these generated receipts so an earlier run cannot satisfy this test.
    for (const FString Name : {FString(TEXT("D2_list.json")), FString(TEXT("D2_call.json"))})
    { IFileManager::Get().Delete(*(FPaths::ProjectSavedDir() / TEXT("KHUTutor") / Name)); }
    FOutputDeviceNull Output;
    TestTrue(TEXT("List console registered"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("KHUTutor.Mcp.List"), Output, nullptr));
    TestTrue(TEXT("Call console registered"), IConsoleManager::Get().ProcessUserConsoleInput(
        TEXT("KHUTutor.Mcp.Call KHUTutorTools.KHUTutorReadToolset.Ping {\"message\":\"hi\"}"), Output, nullptr));
    auto State = MakeShared<FLiveState>();
    IKHUTutorModule::Get().GetMcpClient()->HealthCheck([State](FKHUMcpResult Result) { State->Result = MoveTemp(Result); State->bDone = true; });
    AddCommand(new FLiveMcpCheck(this, State)); return true;
}
#endif

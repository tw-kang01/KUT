#include "AI/KHUAgentLoop.h"
#include "AI/KHUClaudeRuntime.h"
#include "KHUTutorModule.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto RecordFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    enum class ERecordReply { Answer, Fallback, Pause, EditorCall };
    class FRecordChat final : public IKHUChatClient
    {
    public:
        TArray<FKHUChatRequest> Requests;
        TArray<ERecordReply> Replies;
        virtual void Send(FKHUChatRequest Request, FCallback Callback) override
        {
            const auto Reply = Replies.IsValidIndex(Requests.Num()) ? Replies[Requests.Num()] : ERecordReply::Answer;
            Requests.Add(Request);
            FKHUChatResponse R; R.bMock = true; R.bSuccess = Reply == ERecordReply::Answer || Reply == ERecordReply::EditorCall;
            R.bTryNextModel = Reply == ERecordReply::Fallback; R.bIterationLimit = Reply == ERecordReply::Pause;
            R.Error = TEXT("Synthetic record request pause"); R.Content = TEXT("선택한 대화에서 확인된 내용을 기록했습니다.");
            R.Assistant = MakeShared<FJsonObject>(); R.Assistant->SetStringField(TEXT("role"), TEXT("assistant"));
            R.Assistant->SetStringField(TEXT("content"), R.Content);
            if (Reply == ERecordReply::EditorCall)
            {
                const FString ToolName = FKHUToolBridge::FunctionName(FKHUMcpClient::PingToolName());
                const FString Arguments = TEXT("{\"message\":\"record fixture\"}");
                R.Calls.Add({TEXT("record-call"), ToolName, Arguments});
                auto Call = MakeShared<FJsonObject>(), Function = MakeShared<FJsonObject>();
                Call->SetStringField(TEXT("id"), TEXT("record-call")); Call->SetStringField(TEXT("type"), TEXT("function"));
                Function->SetStringField(TEXT("name"), ToolName); Function->SetStringField(TEXT("arguments"), Arguments);
                Call->SetObjectField(TEXT("function"), Function);
                R.Assistant->SetArrayField(TEXT("tool_calls"), {MakeShared<FJsonValueObject>(Call)});
            }
            Callback(MoveTemp(R));
        }
        virtual bool IsMock() const override { return true; }
        virtual bool OwnsHistory() const override { return true; }
        virtual void Cancel() override {}
    };
    class FRecordExecutor final : public IKHUToolExecutor
    {
    public:
        int32 Calls = 0;
        virtual void Execute(const FString&, const TSharedRef<FJsonObject>&, FCallback Callback) override
        { ++Calls; FKHUMcpResult R; R.bSuccess = true; R.Text = TEXT("Synthetic editor result"); Callback(MoveTemp(R)); }
        virtual void Cancel() override {}
    };
    TSharedRef<FKHUAgentLoop> RecordLoop(const FKHUConfig& C, TSharedRef<FRecordChat> Chat, TSharedRef<FRecordExecutor> Executor)
    { return MakeShared<FKHUAgentLoop>(C, Chat, Executor, MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog())); }
    FString RecordUser(const FKHUChatRequest& Request)
    {
        for (int32 I = Request.Messages.Num() - 1; I >= 0; --I)
        { const auto M = Request.Messages[I]->AsObject(); if (M->GetStringField(TEXT("role")) == TEXT("user")) { return M->GetStringField(TEXT("content")); } }
        return TEXT("");
    }
    FString RecordFile()
    { return FPaths::ProjectSavedDir() / TEXT("KHUTutor/RecordContextTests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordContextWire, "KHUTutor.Portfolio.RecordContext.WireArchiveAndOrdinaryTurn", RecordFlags)
bool FRecordContextWire::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Context.bProjectMemory = true;
    const FString Path = RecordFile(); auto Memory = MakeShared<FKHUProjectMemory>(Path, TEXT("RECORD_TRANSPORT_SYNTHETIC"), C.Context);
    auto Chat = MakeShared<FRecordChat>(); auto Executor = MakeShared<FRecordExecutor>(); auto Agent = RecordLoop(C, Chat, Executor);
    Agent->SetProjectMemory(Memory, TEXT(""));
    const FString Question = TEXT("선택한 대화를 바탕으로 문제 해결 과정을 읽기 쉽게 정리해 주세요.");
    const FString Context = TEXT("INTERNAL_RECORD_SENTINEL\n<record_data>[u2] 선택한 과거 학생 보고</record_data>\nEND_RECORD_SENTINEL");
    TestTrue(TEXT("Readable question and hidden context accepted"), Agent->Ask(Question, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false, TEXT(""), Context));
    if (!TestEqual(TEXT("One mock model request"), Chat->Requests.Num(), 1)) { Agent->Cancel(); Memory->Forget(); return false; }
    const auto& Request = Chat->Requests[0]; const FString User = RecordUser(Request);
    TestTrue(TEXT("Wire contains the entire readable question"), User.StartsWith(Question));
    TestTrue(TEXT("Wire contains the entire separate record context"), User.Contains(Context, ESearchCase::CaseSensitive));
    TestTrue(TEXT("Record editor schemas stripped regardless of visible wording"), Request.Tools.IsEmpty() && Request.SerializedTools.IsEmpty());
    FString System, Input; KHUClaudeProtocol::Compose(Request, System, Input);
    TestTrue(TEXT("Managed CLI receives the hidden attachment"), Input.Contains(Context, ESearchCase::CaseSensitive));
    TestFalse(TEXT("Attachment never enters the cached system prefix"), System.Contains(TEXT("INTERNAL_RECORD_SENTINEL")));
    FKHUSavedConversation Saved;
    TestTrue(TEXT("Readable conversation saved"), Memory->ReadConversation(Agent->Receipt()->GetStringField(TEXT("task_id")), 1, Saved));
    TestEqual(TEXT("Conversation goal is only the visible question"), Saved.Goal, Question);
    if (TestTrue(TEXT("Student turn exists"), !Saved.Turns.IsEmpty()))
    { TestEqual(TEXT("Saved student turn is only the visible question"), Saved.Turns[0]->AsObject()->GetStringField(TEXT("text")), Question); }
    FString Disk; TestTrue(TEXT("Local archive readable"), FFileHelper::LoadFileToString(Disk, *Path));
    TestFalse(TEXT("Attachment is not saved as student, goal or coach source"), Disk.Contains(TEXT("INTERNAL_RECORD_SENTINEL")));
    TestFalse(TEXT("Receipt has no hidden request text"), KHUMcpProtocol::WriteObject(Agent->Receipt()).Contains(TEXT("INTERNAL_RECORD_SENTINEL")));
    TestTrue(TEXT("Ordinary follow-up accepted without attachment"), Agent->FollowUp(TEXT("이제 현재 선택한 Blueprint를 확인해 주세요."), 1));
    if (TestEqual(TEXT("Ordinary follow-up dispatched"), Chat->Requests.Num(), 2))
    {
        const auto& Next = Chat->Requests.Last();
        TestFalse(TEXT("Previous attachment not copied into current user turn"), RecordUser(Next).Contains(TEXT("INTERNAL_RECORD_SENTINEL")));
        TestTrue(TEXT("Ordinary follow-up restores editor schemas"), !Next.Tools.IsEmpty() && !Next.SerializedTools.IsEmpty());
        KHUClaudeProtocol::Compose(Next, System, Input);
        TestFalse(TEXT("Managed CLI ordinary turn has no previous attachment"), Input.Contains(TEXT("INTERNAL_RECORD_SENTINEL")));
    }
    TestTrue(TEXT("Follow-up accepts a new independent record attachment"), Agent->FollowUp(TEXT("새로 선택한 대화로 기록을 작성해 주세요."), 1, TEXT("SECOND_RECORD_SENTINEL")));
    if (TestEqual(TEXT("Record follow-up dispatched"), Chat->Requests.Num(), 3))
    {
        const auto& Next = Chat->Requests.Last();
        TestTrue(TEXT("New record attachment arrives"), RecordUser(Next).Contains(TEXT("SECOND_RECORD_SENTINEL")));
        TestFalse(TEXT("Old attachment does not merge with the new record"), RecordUser(Next).Contains(TEXT("INTERNAL_RECORD_SENTINEL")));
        TestTrue(TEXT("Follow-up record mode disables editor tools"), Next.Tools.IsEmpty() && Next.SerializedTools.IsEmpty());
    }
    Agent->Cancel(); Memory->Forget(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordContextResume, "KHUTutor.Portfolio.RecordContext.RetryResumeAndToolRejection", RecordFlags)
bool FRecordContextResume::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig();
    auto& Tier = C.Tiers[TEXT("T1")]; const FString Model = Tier.Chain[0]; Tier.Chain = {Model, Model}; Tier.MaxIters = 10;
    auto Chat = MakeShared<FRecordChat>(); Chat->Replies = {ERecordReply::Fallback, ERecordReply::Pause, ERecordReply::EditorCall, ERecordReply::Answer};
    auto Executor = MakeShared<FRecordExecutor>(); auto Agent = RecordLoop(C, Chat, Executor);
    FKHUToolBridge Bridge(C, FKHUToolExecutorDirect::Catalog()); FString Native, Error; TSharedPtr<FJsonObject> Arguments;
    TestTrue(TEXT("Fixture call is otherwise a valid editor tool"), Bridge.Resolve(FKHUToolBridge::FunctionName(FKHUMcpClient::PingToolName()), TEXT("{\"message\":\"record fixture\"}"), TEXT("T1"), 1, true, Native, Arguments, Error));
    const FString Context = TEXT("RESUME_RECORD_SENTINEL\n선택한 과거 대화 원문 끝");
    Agent->Ask(TEXT("제가 고친 문장으로 기록을 작성해 주세요."), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, TEXT(""), Context);
    TestEqual(TEXT("Fallback and iteration-limited requests reached mock"), Chat->Requests.Num(), 2);
    TestTrue(TEXT("Iteration pause is resumable"), Agent->CanResume()); Agent->Resume();
    TestEqual(TEXT("Resume and rejected tool result complete the record"), Chat->Requests.Num(), 4);
    for (const auto& R : Chat->Requests)
    {
        TestTrue(TEXT("Required attachment survives fallback, resume and model continuation"), RecordUser(R).Contains(Context, ESearchCase::CaseSensitive));
        TestTrue(TEXT("Editor schemas stay disabled for this record turn"), R.Tools.IsEmpty() && R.SerializedTools.IsEmpty());
    }
    TestEqual(TEXT("Unexpected provider tool call never reaches executor"), Executor->Calls, 0);
    const auto Receipt = Agent->Receipt(); const auto& Tools = Receipt->GetArrayField(TEXT("tools"));
    if (TestEqual(TEXT("Rejected call gets a tool receipt"), Tools.Num(), 1))
    {
        TestFalse(TEXT("Tool receipt reports rejection"), Tools[0]->AsObject()->GetBoolField(TEXT("success")));
        TestTrue(TEXT("Record mode is the reason for rejection"), Tools[0]->AsObject()->GetStringField(TEXT("result")).Contains(TEXT("학습 기록")));
    }
    TestTrue(TEXT("Record request completes"), Agent->State() == EKHUAgentState::Idle);
    TestTrue(TEXT("Next new task accepts the ordinary default"), Agent->Ask(TEXT("현재 Blueprint의 연결을 확인해 주세요."), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    if (TestEqual(TEXT("New task dispatched"), Chat->Requests.Num(), 5))
    {
        TestFalse(TEXT("Ask default clears prior record context"), RecordUser(Chat->Requests.Last()).Contains(TEXT("RESUME_RECORD_SENTINEL")));
        TestTrue(TEXT("Ask default restores editor tools"), !Chat->Requests.Last().Tools.IsEmpty());
    }
    Agent->Cancel(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordContextBudget, "KHUTutor.Portfolio.RecordContext.RequiredBudgetAndOptionalRecall", RecordFlags)
bool FRecordContextBudget::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Context.bProjectMemory = true;
    auto Memory = MakeShared<FKHUProjectMemory>(RecordFile(), TEXT("RECORD_BUDGET_SYNTHETIC"), C.Context);
    const FString Question = TEXT("이 대화로 문제 해결 기록을 작성해 주세요.");
    const FString Context = TEXT("REQUIRED_RECORD_BEGIN\n") + FString::ChrN(1000, TCHAR(0xAC00)) + TEXT("\nREQUIRED_RECORD_END");
    const auto MakeLoop = [&Memory](const FKHUConfig& Settings, TSharedRef<FRecordChat> Chat)
    { auto Loop = RecordLoop(Settings, Chat, MakeShared<FRecordExecutor>()); Loop->SetProjectMemory(Memory, TEXT("")); return Loop; };
    auto BaselineChat = MakeShared<FRecordChat>(); auto Baseline = MakeLoop(C, BaselineChat);
    Baseline->Ask(Question, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false, TEXT(""), Context);
    if (!TestEqual(TEXT("Baseline full attachment fits"), BaselineChat->Requests.Num(), 1)) { Baseline->Cancel(); Memory->Forget(); return false; }
    const int32 RequiredTokens = BaselineChat->Requests[0].EstimatedInputTokens;
    C.Tiers[TEXT("T1")].InputBudget = RequiredTokens + 200;
    for (const TCHAR* Kind : {TEXT("goal"), TEXT("decision"), TEXT("attempt"), TEXT("open"), TEXT("next")})
    { TestTrue(TEXT("Optional historical note saved"), Memory->Remember(TEXT(""), Kind, TEXT("OPTIONAL_RECORD_NOTE ") + FString::ChrN(1000, TCHAR(0xAC00)), 1)); }
    auto Chat = MakeShared<FRecordChat>(); auto Agent = MakeLoop(C, Chat);
    Agent->Ask(Question, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false, TEXT(""), Context);
    if (TestEqual(TEXT("Optional recall is discarded before required record input"), Chat->Requests.Num(), 1))
    {
        const FString User = RecordUser(Chat->Requests[0]);
        TestTrue(TEXT("Full attachment preserved after recall fallback"), User.Contains(Context, ESearchCase::CaseSensitive));
        TestTrue(TEXT("Recall omission explicit"), User.Contains(TEXT("PROJECT_PROGRESS_OMITTED")));
        TestFalse(TEXT("Optional note removed"), User.Contains(TEXT("OPTIONAL_RECORD_NOTE")));
        TestTrue(TEXT("Attachment included in enforced token total"), Chat->Requests[0].EstimatedInputTokens <= C.Tiers[TEXT("T1")].InputBudget);
    }
    auto TooLargeChat = MakeShared<FRecordChat>(); auto TooLarge = MakeLoop(C, TooLargeChat);
    const FString LargerContext = Context + FString::ChrN(6000, TCHAR(0xAC00)) + TEXT("NEVER_TRUNCATE_RECORD_TAIL");
    TestTrue(TEXT("Valid-length larger context enters budget validation"), TooLarge->Ask(Question, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false, TEXT(""), LargerContext));
    TestEqual(TEXT("Over-budget attachment causes no model dispatch"), TooLargeChat->Requests.Num(), 0);
    TestTrue(TEXT("Budget failure is explained"), TooLarge->Receipt()->GetStringField(TEXT("text")).Contains(TEXT("문맥 한도")));
    Baseline->Cancel(); Agent->Cancel(); TooLarge->Cancel(); Memory->Forget(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordContextBounds, "KHUTutor.Portfolio.RecordContext.InputBounds", RecordFlags)
bool FRecordContextBounds::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Tiers[TEXT("T1")].InputBudget = 100000;
    auto Chat = MakeShared<FRecordChat>(); auto Agent = RecordLoop(C, Chat, MakeShared<FRecordExecutor>());
    const FString Question = FString::ChrN(16000, TCHAR('Q')), Context = FString::ChrN(32000, TCHAR('R'));
    TestFalse(TEXT("Oversized context rejected before starting a task"), Agent->Ask(TEXT("기록 작성"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false, TEXT(""), Context + TEXT("R")));
    TestEqual(TEXT("Invalid context dispatches no request"), Chat->Requests.Num(), 0);
    TestTrue(TEXT("Separate question and attachment caps accepted exactly"), Agent->Ask(Question, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false, TEXT(""), Context));
    if (TestEqual(TEXT("Both maximum lengths arrive intact"), Chat->Requests.Num(), 1))
    {
        TestTrue(TEXT("Question cap remains 16000"), RecordUser(Chat->Requests[0]).StartsWith(Question));
        TestTrue(TEXT("Context cap retains all 32000 characters"), RecordUser(Chat->Requests[0]).Contains(Context, ESearchCase::CaseSensitive));
    }
    TestFalse(TEXT("Follow-up context cannot exceed the same cap"), Agent->FollowUp(TEXT("이어서 기록"), 1, Context + TEXT("R")));
    TestFalse(TEXT("Follow-up question cap remains independent"), Agent->FollowUp(Question + TEXT("Q"), 1));
    TestEqual(TEXT("Rejected follow-ups dispatch no requests"), Chat->Requests.Num(), 1);
    Agent->Cancel(); return true;
}
#endif

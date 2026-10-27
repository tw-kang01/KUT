#include "UI/KHUEditorObservation.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "GraphEditAction.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "AI/KHUHintPolicy.h"
#include "AI/KHUAgentLoop.h"
#include "AI/KHUSecretStore.h"
#include "UI/KHUConversationText.h"
#include "UI/KHUApprovalGate.h"
#include "UI/KHUTutorSettings.h"
#include "UI/KHUTutorStyle.h"
#include "UI/SKHUTutorPanel.h"
#include "KHUTutorModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformTime.h"
#include "Framework/Application/SlateApplication.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    class FChat final : public IKHUChatClient
    {
    public:
        TArray<FKHUChatRequest> Requests;
        virtual void Send(FKHUChatRequest R, FCallback Callback) override
        {
            Requests.Add(MoveTemp(R)); FKHUChatResponse Reply; Reply.bSuccess = Reply.bMock = true;
            Reply.Content = TEXT("테스트 답변"); Reply.Assistant = MakeShared<FJsonObject>(); Reply.Assistant->SetStringField(TEXT("role"), TEXT("assistant")); Reply.Assistant->SetStringField(TEXT("content"), Reply.Content);
            Callback(MoveTemp(Reply));
        }
        virtual void Cancel() override {}
        virtual bool IsMock() const override { return true; }
    };
    class FNoTools final : public IKHUToolExecutor
    {
    public:
        virtual void Execute(const FString&, const TSharedRef<FJsonObject>&, FCallback Callback) override { Callback(FKHUMcpResult()); }
        virtual void Cancel() override {}
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8HintCaps, "KHUTutor.UI.HintPolicy", Flags)
bool FD8HintCaps::RunTest(const FString&)
{
    const int32 Expected[] = {250, 400, 600, 1000, 1500};
    for (int32 L = 0; L <= 4; ++L)
    { TestEqual(TEXT("Per-level cap"), FKHUHintPolicy::OutputTokens(L, false), Expected[L]); TestEqual(TEXT("Reasoning floor"), FKHUHintPolicy::OutputTokens(L, true), 1500); TestTrue(TEXT("Explicit hint note"), FKHUHintPolicy::SystemNote(L, TEXT("T1")).Contains(FString::Printf(TEXT("HINT LEVEL: L%d"), L))); }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8ToolPolicy, "KHUTutor.UI.L0NoShowOrAct", Flags)
bool FD8ToolPolicy::RunTest(const FString&)
{
    const auto C = IKHUTutorModule::Get().PanelConfig(); FKHUToolBridge Bridge(C, FKHUToolExecutorDirect::Catalog());
    for (const auto& V : Bridge.Definitions(TEXT("T1"), 0, true))
    { TestFalse(TEXT("L0 denies Show even when caller opts in"), V->AsObject()->GetObjectField(TEXT("function"))->GetStringField(TEXT("name")).Contains(TEXT("ShowToolset"))); }
    FString Native, Error; TSharedPtr<FJsonObject> Args;
    TestFalse(TEXT("Forged L0 Show rejected"), Bridge.Resolve(TEXT("KHUTutorTools_KHUTutorShowToolset_FocusActor"), TEXT("{\"actorLabel\":\"Door\"}"), TEXT("T1"), 0, true, Native, Args, Error));
    for (int32 L = 0; L <= 4; ++L)
    { for (const auto& V : Bridge.Definitions(TEXT("T1"), L, true)) { TestFalse(TEXT("No premature Act"), V->AsObject()->GetObjectField(TEXT("function"))->GetStringField(TEXT("name")).Contains(TEXT("ActToolset"))); } }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Followup, "KHUTutor.UI.FollowUpRetainsEvidence", Flags)
bool FD8Followup::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Tiers[TEXT("T1")].ReasoningEffort.Reset(); auto Chat = MakeShared<FChat>();
    auto Loop = MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FNoTools>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    TestTrue(TEXT("Initial task"), Loop->Ask(TEXT("처음 질문"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    TestTrue(TEXT("Escalation is a follow-up"), Loop->FollowUp(TEXT("더 설명"), 3));
    TestEqual(TEXT("Two calls"), Chat->Requests.Num(), 2);
    if (Chat->Requests.Num() == 2)
    {
        TestEqual(TEXT("L1 cap"), Chat->Requests[0].MaxTokens, 400); TestEqual(TEXT("L3 cap"), Chat->Requests[1].MaxTokens, 1000);
        int32 Notes = 0; bool FirstQuestion = false, OldAnswer = false;
        for (const auto& V : Chat->Requests[1].Messages)
        { const FString Text = V->AsObject()->GetStringField(TEXT("content")); Notes += Text.Contains(TEXT("HINT LEVEL:")) ? 1 : 0; FirstQuestion |= Text == TEXT("처음 질문"); OldAnswer |= Text == TEXT("테스트 답변"); }
        TestEqual(TEXT("Only latest teaching note retained"), Notes, 1); TestTrue(TEXT("Previous turn retained"), FirstQuestion && OldAnswer);
    }
    Loop->Cancel(); TestFalse(TEXT("Canceled incomplete task cannot continue"), Loop->FollowUp(TEXT("뒤늦은 요청"), 1)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Escape, "KHUTutor.UI.UntrustedMarkup", Flags)
bool FD8Escape::RunTest(const FString&)
{
    const FString Out = KHUConversationText::Render(TEXT("<a id=\"khu\" target=\"0\">#2</> **굵게** `Target`"), {});
    TestFalse(TEXT("Model cannot inject hyperlinks"), Out.Contains(TEXT("<a "))); TestTrue(TEXT("Markup escaped"), Out.Contains(TEXT("&lt;a")));
    TestTrue(TEXT("Bold"), Out.Contains(TEXT("<b>굵게</>"))); TestTrue(TEXT("Code"), Out.Contains(TEXT("<code>Target</>"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Links, "KHUTutor.UI.EvidenceLinks", Flags)
bool FD8Links::RunTest(const FString&)
{
    TMap<FString, FString> Map; Map.Add(TEXT("#1"), TEXT("0")); Map.Add(TEXT("#10"), TEXT("1")); Map.Add(TEXT("@Door"), TEXT("2"));
    const FString Out = KHUConversationText::Render(TEXT("#1 #10 #100 @Door @Door2 #9"), Map);
    TestTrue(TEXT("Long node gets exact binding"), Out.Contains(TEXT("target=\"1\">#10</>")));
    TestFalse(TEXT("No prefix binding"), Out.Contains(TEXT("</>00"))); TestFalse(TEXT("No actor prefix binding"), Out.Contains(TEXT("</>2")));
    TestTrue(TEXT("Unknown nodes remain plain"), Out.EndsWith(TEXT("#9")));
    TMap<FString, FString> Paths; Paths.Add(TEXT("@/Game/Map.Map:PersistentLevel.Door"), TEXT("3"));
    const FString PathOut = KHUConversationText::Render(TEXT("@/Game/Map.Map:PersistentLevel.Door.Component"), Paths);
    TestFalse(TEXT("Actor path is not a prefix link for component identity"), PathOut.Contains(TEXT("<a ")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Approval, "KHUTutor.UI.ApprovalFailsClosed", Flags)
bool FD8Approval::RunTest(const FString&)
{ TestFalse(TEXT("Empty preview never approved"), FKHUApprovalGate::Confirm(FText::GetEmpty())); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Defaults, "KHUTutor.UI.UserDefaults", Flags)
bool FD8Defaults::RunTest(const FString&)
{
    const auto C = IKHUTutorModule::Get().PanelConfig();
    TestEqual(TEXT("User requested Claude default model"), C.Tiers[TEXT("T1")].Chain[0], FString(TEXT("claude-sonnet-5-5")));
    TestTrue(TEXT("Native Claude panel enabled"), C.bClaudePanel);
    TestEqual(TEXT("Actual provider requires an explicit supported reasoning setting"), C.Tiers[TEXT("T1")].ReasoningEffort, FString(TEXT("low")));
    TestFalse(TEXT("Reasoning request omits temperature"), C.Tiers[TEXT("T1")].Temperature.IsSet());
    TestTrue(TEXT("Observed credit conversion exists"), C.Cost(TEXT("gpt-6.1-sol"), 17, 0, 6).IsSet());
    TestTrue(TEXT("Correct conversion"), FMath::IsNearlyEqual(C.Cost(TEXT("gpt-6.1-sol"), 17, 0, 6).GetValue(), 0.094));
    TestEqual(TEXT("Per-user settings container"), GetDefault<UKHUTutorSettings>()->GetContainerName(), FName(TEXT("Editor"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Widgets, "KHUTutor.UI.WidgetConstructionAndShutdown", Flags)
bool FD8Widgets::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate unavailable; construction not verified")); return false; }
    auto Panel = SNew(SKHUTutorPanel); Panel->Stop(); Panel->Stop();
    TestTrue(TEXT("Panel construction and idempotent shutdown completed"), Panel->GetChildren()->Num() > 0); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChatSidebarPages, "KHUTutor.UI.Sidebar.PagesKeepDraftAndConversation", Flags)
bool FChatSidebarPages::RunTest(const FString&)
{
    auto Panel = SNew(SKHUTutorPanel);
    const auto Conversation = Panel->Conversation;
    const int32 Messages = Panel->MessageCount;
    Panel->Input->SetText(FText::FromString(TEXT("작성 중인 질문")));
    TestEqual(TEXT("Chat is the default page"), Panel->Pages->GetActiveWidgetIndex(), 0);
    for (int32 Page : {1, 2, 3, 4, 0}) { Panel->ShowPage(Page); TestEqual(TEXT("Requested page is active"), Panel->Pages->GetActiveWidgetIndex(), Page); }
    TestEqual(TEXT("Switching pages preserves the unsent draft"), Panel->Input->GetText().ToString(), FString(TEXT("작성 중인 질문")));
    TestTrue(TEXT("Conversation widget is retained"), Conversation == Panel->Conversation);
    TestEqual(TEXT("Navigation creates no extra chat messages"), Panel->MessageCount, Messages);
    Panel->ShowPage(99); TestEqual(TEXT("Invalid page stays within range"), Panel->Pages->GetActiveWidgetIndex(), 4);
    Panel->Stop(); Panel->ShowPage(0); TestEqual(TEXT("Stopped panel cannot navigate"), Panel->Pages->GetActiveWidgetIndex(), 4);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChatSidebarResume, "KHUTutor.UI.Sidebar.NavigationKeepsTaskAndStopReleasesIt", Flags)
bool FChatSidebarResume::RunTest(const FString&)
{
    auto Panel = SNew(SKHUTutorPanel); Panel->bMock = true;
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Tiers[TEXT("T1")].ReasoningEffort.Reset();
    auto Chat = MakeShared<FChat>();
    auto Loop = MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FNoTools>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    TestTrue(TEXT("Mock conversation starts"), Loop->Ask(TEXT("처음 질문"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    Panel->Loop = Loop; Panel->bHasTask = true;
    Panel->ShowPage(1); Panel->ShowPage(2); Panel->ShowPage(0);
    TestTrue(TEXT("Task is retained while reading records/settings"), Panel->Loop == Loop && Panel->bHasTask);
    TestEqual(TEXT("Navigation makes no model request"), Chat->Requests.Num(), 1);
    Panel->Submit(TEXT("이어서 질문"), false);
    TestEqual(TEXT("Follow-up uses the same conversation"), Chat->Requests.Num(), 2);
    Panel->CancelTask();
    TestFalse(TEXT("Stop releases the active loop"), Panel->Loop.IsValid());
    TestFalse(TEXT("Stop does not leave the old task resumable"), Panel->bHasTask);
    TestTrue(TEXT("Stop explains the next action"), Panel->Status.Contains(TEXT("새 질문")));
    Panel->Stop(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPauseNoticeButtons, "KHUTutor.UI.PauseNotice.IterationSoftHardAndActiveGuards", Flags)
bool FPauseNoticeButtons::RunTest(const FString&)
{
    // Test rendered notice attributes and click delegates, not just Loop::Resume.
    class FDeferredChat final : public IKHUChatClient
    {
    public:
        int32 Requests = 0; FCallback Pending;
        virtual void Send(FKHUChatRequest, FCallback Callback) override { ++Requests; Pending = MoveTemp(Callback); }
        virtual void Cancel() override { Pending = {}; }
        virtual bool IsMock() const override { return false; } // Budgets enforced, no network calls.
        void Reply(bool bLimit)
        {
            FKHUChatResponse R; R.bSuccess = !bLimit; R.bIterationLimit = bLimit;
            R.Error = TEXT("Iteration pause fixture"); R.Attempts = 1; R.Credits = bLimit ? 0.0 : 1.0;
            R.Content = TEXT("Fixture answer"); R.Assistant = MakeShared<FJsonObject>();
            R.Assistant->SetStringField(TEXT("role"), TEXT("assistant")); R.Assistant->SetStringField(TEXT("content"), R.Content);
            auto Callback = MoveTemp(Pending); Callback(MoveTemp(R));
        }
    };
    TFunction<bool(TSharedRef<SWidget>, const FString&)> HasLabel;
    HasLabel = [&HasLabel](TSharedRef<SWidget> Widget, const FString& Label)
    {
        if (Widget->GetTypeAsString() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Label) { return true; }
        auto Children = Widget->GetChildren();
        for (int32 I = 0; I < Children->Num(); ++I) { if (HasLabel(Children->GetChildAt(I), Label)) { return true; } }
        return false;
    };
    TFunction<TSharedPtr<SButton>(TSharedRef<SWidget>, const FString&)> FindButton;
    FindButton = [&FindButton, &HasLabel](TSharedRef<SWidget> Widget, const FString& Label) -> TSharedPtr<SButton>
    {
        if (Widget->GetTypeAsString() == TEXT("SButton") && HasLabel(Widget, Label)) { return StaticCastSharedRef<SButton>(Widget); }
        auto Children = Widget->GetChildren();
        for (int32 I = Children->Num() - 1; I >= 0; --I) { if (auto Found = FindButton(Children->GetChildAt(I), Label)) { return Found; } }
        return nullptr;
    };
    const auto Enabled = [](const TSharedPtr<SButton>& Button)
    { Button->SlatePrepass(); return Button->IsEnabled(); };
    // 0 = runtime iteration cap; 1 = soft budget; 2 = hard budget.
    for (int32 Case = 0; Case < 3; ++Case)
    {
        auto Panel = SNew(SKHUTutorPanel);
        auto C = IKHUTutorModule::Get().PanelConfig();
        C.Tiers[TEXT("T1")].Chain = {TEXT("pause_notice_fixture")}; C.Tiers[TEXT("T1")].ReasoningEffort.Reset();
        C.Tiers[TEXT("T1")].Api = TEXT("chat_completions");
        FKHUPrice Price; Price.Input = Price.CachedInput = 0; Price.Output = 1; Price.CreditsPerUsd = 1000000;
        C.Prices.Add(TEXT("pause_notice_fixture"), Price); C.OutputHardLimit = 1;
        C.PerTaskCredits = 1; C.PerTaskHardCredits = Case == 2 ? 1 : 2;
        auto Chat = MakeShared<FDeferredChat>();
        auto Agent = MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FNoTools>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
        Panel->Loop = Agent; Panel->bHasTask = true; Panel->bApiConnected = true;
        TWeakPtr<SKHUTutorPanel> WeakPanel = Panel;
        Agent->Ask(TEXT("first"), TEXT("T1"), 1, [WeakPanel](EKHUAgentState State, const FString& Text)
        { if (auto P = WeakPanel.Pin()) { P->OnEvent(State, Text); } });
        Chat->Reply(Case == 0);
        if (Case != 0) { Agent->FollowUp(TEXT("second"), 1); }
        TestTrue(TEXT("Actual loop is paused"), Agent->State() == EKHUAgentState::AskContinue);
        TestTrue(TEXT("Paused task still blocks unrelated submissions"), Panel->Busy());
        TestTrue(TEXT("Status describes the pause"), Panel->MetaLine().ToString().Contains(TEXT("일시정지")));
        auto Continue = FindButton(Panel->Conversation.ToSharedRef(), TEXT("계속하기"));
        auto StopHere = FindButton(Panel->Conversation.ToSharedRef(), TEXT("여기서 멈추기"));
        if (!TestTrue(TEXT("Notice contains both buttons"), Continue.IsValid() && StopHere.IsValid())) { Panel->Stop(); continue; }
        TestEqual(TEXT("Only resumable pauses enable Continue"), Enabled(Continue), Case != 2);
        TestTrue(TEXT("Every pause enables Stop here"), Enabled(StopHere));
        const int32 Before = Chat->Requests;
        Panel->Submit(TEXT("unrelated question while paused"), false);
        TestEqual(TEXT("Paused submit cannot make a duplicate request"), Chat->Requests, Before);
        if (Case != 2)
        {
            if (Enabled(Continue)) { Continue->SimulateClick(); }
            TestEqual(TEXT("Continue sends exactly one request"), Chat->Requests, Before + 1);
            TestTrue(TEXT("Resumed request is active"), Agent->State() == EKHUAgentState::WaitingModel);
            TestFalse(TEXT("Old Continue is disabled while active"), Enabled(Continue));
            TestFalse(TEXT("Pause-only Stop is disabled while active"), Enabled(StopHere));
            Panel->Submit(TEXT("duplicate while active"), false);
            TestEqual(TEXT("Active submit cannot duplicate the request"), Chat->Requests, Before + 1);
            // Return to a pause only if the enabled button actually resumed the request.
            if (Chat->Pending) { Chat->Reply(true); }
            StopHere = FindButton(Panel->Conversation.ToSharedRef(), TEXT("여기서 멈추기"));
        }
        if (Enabled(StopHere)) { StopHere->SimulateClick(); }
        TestFalse(TEXT("Stop releases the task"), Panel->Loop.IsValid() || Panel->bHasTask);
        TestFalse(TEXT("Old Continue is disabled after stop"), Enabled(Continue));
        TestFalse(TEXT("Old Stop is disabled after stop"), Enabled(StopHere));
        Panel->Stop();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChatSidebarMessages, "KHUTutor.UI.Sidebar.AuthenticationOpensSettingsWithoutSending", Flags)
bool FChatSidebarMessages::RunTest(const FString&)
{
    auto Panel = SNew(SKHUTutorPanel); Panel->bApiConnected = false;
    const int32 Messages = Panel->MessageCount;
    Panel->Input->SetText(FText::FromString(TEXT("처음 질문")));
    Panel->Submit(Panel->Input->GetText().ToString(), false);
    TestEqual(TEXT("Missing connection opens settings"), Panel->Pages->GetActiveWidgetIndex(), 2);
    TestEqual(TEXT("Unsent question is not discarded"), Panel->Input->GetText().ToString(), FString(TEXT("처음 질문")));
    TestFalse(TEXT("No paid loop was created"), Panel->Loop.IsValid());
    TestEqual(TEXT("Question is not presented as sent"), Panel->MessageCount, Messages);
    Panel->Stop(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8ProviderError, "KHUTutor.UI.ProviderErrorRedacted", Flags)
bool FD8ProviderError::RunTest(const FString&)
{
    class FTransport final : public IKHUMcpTransport
    {
    public:
        virtual FCancel Send(const FKHUMcpHttpRequest& Request, FCompletion Callback) override
        {
            FKHUMcpHttpResponse R; R.Status = Request.Verb == TEXT("GET") ? 200 : 400;
            R.Body = R.Status == 200 ? TEXT("{\"data\":[{\"id\":\"fixture_model\",\"type\":\"llm\",\"endpoints\":[\"/v1/gateway/chat/completions/\"]}]}")
                : TEXT("{\"error\":\"reasoning invalid fixture_api_secret\"}");
            Callback(MoveTemp(R)); return [] {};
        }
    };
    auto C = IKHUTutorModule::Get().PanelConfig();
    auto Client = MakeShared<FKHUChatClient>(C, TEXT("fixture_api_secret"), MakeShared<FTransport>());
    FKHUChatRequest R; R.Model = TEXT("fixture_model"); R.MaxTokens = 400; R.OutputHardLimit = 1500; R.RemainingCredits = 100; R.AttemptCreditReserve = 1;
    bool Called = false;
    Client->Send(R, [this, &Called](FKHUChatResponse Reply)
    {
        Called = true; TestEqual(TEXT("Terminal provider status preserved"), Reply.HttpStatus, 400);
        TestFalse(TEXT("Credential never enters error receipt"), Reply.ProviderError.Contains(TEXT("fixture_api_secret")));
        TestTrue(TEXT("Actionable provider detail retained"), Reply.ProviderError.Contains(TEXT("reasoning invalid")));
        TestTrue(TEXT("Explicit redaction marker"), Reply.ProviderError.Contains(TEXT("API_KEY_REDACTED")));
    });
    TestTrue(TEXT("Transport completed"), Called); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD8Live, "KHUTutor.UI.Live.DefaultModelLocalTools", Flags)
bool FD8Live::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorD8Live"))) { AddInfo(TEXT("Paid default-model test not requested")); return true; }
    FString Error; auto Loop = IKHUTutorModule::Get().CreatePanelLoop(false, Error); if (!Loop) { AddError(Error); return false; }
    TestTrue(TEXT("Panel live request started"), Loop->Ask(TEXT("반드시 Ping 도구를 한 번 호출하세요. message는 d8-default-live 입니다. 결과의 프로젝트 이름을 한국어 한 문장으로 알려 주세요. 다른 도구는 호출하지 마세요."), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, false));
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FCheck(FAutomationTestBase* InTest, TSharedRef<FKHUAgentLoop> InLoop) : Test(InTest), Agent(InLoop), Deadline(FPlatformTime::Seconds() + 180) {}
        virtual bool Update() override
        {
            if (Agent->State() != EKHUAgentState::Idle && Agent->State() != EKHUAgentState::AskContinue && FPlatformTime::Seconds() < Deadline) { return false; }
            const auto R = Agent->Receipt(); const auto& Tools = R->GetArrayField(TEXT("tools")); const auto& Usage = R->GetArrayField(TEXT("usage"));
            Test->TestTrue(TEXT("Default model -> owned local tool -> Korean answer"), Agent->State() == EKHUAgentState::Idle && Tools.Num() == 1 && Tools[0]->AsObject()->GetBoolField(TEXT("success")) && Tools[0]->AsObject()->GetStringField(TEXT("result")).Contains(TEXT("PONG: d8-default-live")) && R->GetStringField(TEXT("text")).Contains(TEXT("TutorSandbox")));
            Test->TestTrue(TEXT("At least two real responses"), Usage.Num() >= 2 && !Usage[0]->AsObject()->GetBoolField(TEXT("mock")));
            Test->TestTrue(TEXT("Receipt saved"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(R), *(FPaths::ProjectSavedDir() / TEXT("KHUTutor/d8_local_live_result.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            Agent->Cancel(); return true;
        }
    private: FAutomationTestBase* Test; TSharedRef<FKHUAgentLoop> Agent; double Deadline;
    };
    AddCommand(new FCheck(this, Loop.ToSharedRef())); return true;
}

namespace
{
    struct FObservationFixture
    {
        UBlueprint* BP = NewObject<UBlueprint>();
        UEdGraph* Graph = nullptr;
        UEdGraphNode* A = nullptr;
        UEdGraphNode* B = nullptr;
        FObservationFixture()
        {
            BP->AddToRoot(); Graph = NewObject<UEdGraph>(BP, TEXT("EventGraph")); BP->UbergraphPages.Add(Graph);
            A = NewObject<UEdGraphNode>(Graph); A->NodeGuid = FGuid(0,0,0,1); Graph->Nodes.Add(A);
            B = NewObject<UEdGraphNode>(Graph); B->NodeGuid = FGuid(0,0,0,2); Graph->Nodes.Add(B);
        }
        ~FObservationFixture() { BP->RemoveFromRoot(); }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FU1ObservationScope, "KHUTutor.UI.Observation.SelectionAndOwnership", Flags)
bool FU1ObservationScope::RunTest(const FString&)
{
    FObservationFixture F, Other;
    FKHUEditorObservation None(F.BP, F.Graph, true, {});
    TestFalse(TEXT("No selection fails closed, no all-graph fallback"), None.IsCurrent());
    FKHUEditorObservation Selected(F.BP, F.Graph, true, {F.A, Other.A});
    TestTrue(TEXT("Selected scope captured"), Selected.IsCurrent());
    TestEqual(TEXT("Foreign graph node excluded"), Selected.SelectedCount, 1);
    TestTrue(TEXT("Only one node and zero expansion"), Selected.Digest.Contains(TEXT("shown=1 focus=selection hops=0")));
    TestFalse(TEXT("Unselected node row excluded"), Selected.Digest.Contains(TEXT("\n#2 ")));
    FKHUEditorObservation All(F.BP, F.Graph, false, {});
    TestTrue(TEXT("Whole graph scope is explicit"), All.Digest.Contains(TEXT("shown=2 focus=all hops=0")));
    FKHUEditorObservation Wrong(F.BP, Other.Graph, false, {});
    TestFalse(TEXT("Cross-Blueprint context rejected"), Wrong.IsCurrent());
    TSharedPtr<FJsonObject> Data;
    TestTrue(TEXT("Observation is JSON data"), KHUMcpProtocol::ReadObject(Selected.Json(), Data));
    if (Data) { TestTrue(TEXT("Editor not runtime claim"), Data->GetStringField(TEXT("world")).Contains(TEXT("not a PIE"))); }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FU1ObservationInvalidation, "KHUTutor.UI.Observation.EditAndRefresh", Flags)
bool FU1ObservationInvalidation::RunTest(const FString&)
{
    FObservationFixture F;
    TSharedPtr<FKHUEditorObservation> Before = MakeShared<FKHUEditorObservation>(F.BP, F.Graph, false, TArray<UEdGraphNode*>());
    F.Graph->SelectNodeSet({F.A}, true);
    TestTrue(TEXT("Navigation selection preserves snapshot"), Before->IsCurrent());
    F.Graph->NotifyGraphChanged();
    TestFalse(TEXT("Edits expire old evidence"), Before->IsCurrent());
    TSharedPtr<FKHUEditorObservation> After = MakeShared<FKHUEditorObservation>(F.BP, F.Graph, false, TArray<UEdGraphNode*>());
    TestTrue(TEXT("Explicit reread yields fresh evidence"), After->IsCurrent());
    TestFalse(TEXT("Reread cannot revive old links"), Before->IsCurrent());
    Before.Reset(); After.Reset(); F.Graph->NotifyGraphChanged();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FU1ObservationConversation, "KHUTutor.UI.Observation.FollowUpTransport", Flags)
bool FU1ObservationConversation::RunTest(const FString&)
{
    auto C = IKHUTutorModule::Get().PanelConfig(); auto Chat = MakeShared<FChat>();
    auto Loop = MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FNoTools>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    Loop->SetEditorObservation(TEXT("{\"graph_digest\":\"BEFORE_UNIQUE\"}"));
    TestTrue(TEXT("Initial observation sent"), Loop->Ask(TEXT("처음 질문"), TEXT("T1"), 0, [](EKHUAgentState,const FString&) {}));
    Loop->SetEditorObservation(TEXT("{\"graph_digest\":\"AFTER_UNIQUE\"}"));
    TestTrue(TEXT("Refreshed follow-up sent"), Loop->FollowUp(TEXT("수정 후 확인"), 0));
    TestEqual(TEXT("No extra grading/model request"), Chat->Requests.Num(), 2);
    if (Chat->Requests.Num() == 2)
    {
        FString Latest;
        for (const auto& V : Chat->Requests[1].Messages)
        {
            const auto O = V->AsObject(); const FString Text = O->GetStringField(TEXT("content"));
            if (O->GetStringField(TEXT("role")) == TEXT("user")) { Latest = Text; }
            if (Text.Contains(TEXT("AFTER_UNIQUE"))) { TestEqual(TEXT("Editor data never system role"), O->GetStringField(TEXT("role")), FString(TEXT("user"))); }
        }
        TestTrue(TEXT("Latest turn contains refreshed evidence"), Latest.Contains(TEXT("AFTER_UNIQUE")));
        TestFalse(TEXT("Latest turn does not resend old snapshot as current"), Latest.Contains(TEXT("BEFORE_UNIQUE")));
    }
    Loop->Cancel(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStuckHintOffer, "KHUTutor.UI.StuckBeginnerHintOffer", Flags)
bool FStuckHintOffer::RunTest(const FString&)
{
    // Markers are conservative; an ordinary question or answer ends the streak.
    TestTrue(TEXT("Don't-know marker"), FKHUStuckStreak::IsStuck(TEXT("모르겠어요.")));
    TestTrue(TEXT("Too-hard marker"), FKHUStuckStreak::IsStuck(TEXT("그것도 잘 모르겠어요... 너무 어려워요.")));
    TestFalse(TEXT("Normal answer is not stuck"), FKHUStuckStreak::IsStuck(TEXT("문이 위로 열릴 것 같아요.")));
    FKHUStuckStreak S; S.Note(TEXT("모르겠어요")); TestFalse(TEXT("One stuck message is not enough"), S.TakeOffer(0));
    S.Note(TEXT("문이 안 움직여요")); S.Note(TEXT("몰라요")); TestFalse(TEXT("A normal message resets the streak"), S.TakeOffer(0));
    S.Note(TEXT("어려워요")); TestFalse(TEXT("Never offered above L0"), S.TakeOffer(1));
    TestTrue(TEXT("Second consecutive stuck message at L0 offers"), S.TakeOffer(0)); TestFalse(TEXT("Offered once per streak"), S.TakeOffer(0));
    S.Note(TEXT("또 모르겠어요")); TestFalse(TEXT("Continuing streak does not offer again"), S.TakeOffer(0));

    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate required for panel test")); return false; }
    auto Panel = SNew(SKHUTutorPanel); Panel->Hint = 0;
    Panel->Stuck.Note(TEXT("하나도 모르겠어요")); Panel->Stuck.Note(TEXT("모르겠어요"));
    const int32 Before = Panel->MessageCount;
    Panel->OnEvent(EKHUAgentState::Idle, TEXT("화면에서 무엇이 보였나요?"));
    TestEqual(TEXT("Answer plus one hint-offer card"), Panel->MessageCount, Before + 2);
    TestEqual(TEXT("Card does not change the level"), Panel->Hint, 0);
    Panel->OnEvent(EKHUAgentState::Idle, TEXT("문은 어디에 있나요?"));
    TestEqual(TEXT("No second card in the same streak"), Panel->MessageCount, Before + 3);
    Panel->ResetTask(false); Panel->Hint = 1; Panel->Stuck.Note(TEXT("모르겠어요")); Panel->Stuck.Note(TEXT("몰라요"));
    const int32 Upper = Panel->MessageCount; Panel->OnEvent(EKHUAgentState::Idle, TEXT("이 노드를 보세요."));
    TestEqual(TEXT("No card above L0"), Panel->MessageCount, Upper + 1);
    Panel->Stop(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FU1PanelState, "KHUTutor.UI.CompactStatusAndExpiredLinks", Flags)
bool FU1PanelState::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate required for panel test")); return false; }
    auto Panel = SNew(SKHUTutorPanel); const int32 Before = Panel->MessageCount;
    const FString LongReply = FString::ChrN(1000, TCHAR('X'));
    Panel->OnEvent(EKHUAgentState::Idle, LongReply);
    TestEqual(TEXT("Answer appended exactly once"), Panel->MessageCount, Before + 1);
    TestTrue(TEXT("Status stays short"), Panel->Status.Len() < 100);
    TestFalse(TEXT("Answer not duplicated in status"), Panel->Status.Contains(LongReply));
    FObservationFixture F;
    auto Evidence = MakeShared<FKHUEditorObservation>(F.BP, F.Graph, false, TArray<UEdGraphNode*>());
    SKHUTutorPanel::FLink Link; Link.Observation = Evidence; Link.Args = MakeShared<FJsonObject>();
    Panel->Links.Add(Link); F.Graph->NotifyGraphChanged();
    const int32 Messages = Panel->MessageCount;
    Panel->Hint = 0; Panel->Navigate(TEXT("0"));
    TestEqual(TEXT("L0 navigation disabled"), Panel->MessageCount, Messages);
    Panel->Hint = 1; Panel->Navigate(TEXT("0"));
    TestEqual(TEXT("Expired link yields notice instead of tool execution"), Panel->MessageCount, Messages + 1);
    Panel->ResetTask(false);
    TestEqual(TEXT("New task removes links"), Panel->Links.Num(), 0);
    TestFalse(TEXT("New task removes snapshot"), Panel->Observation.IsValid());
    TestFalse(TEXT("New task status is not prior answer"), Panel->Status.Contains(LongReply));
    Panel->Stop(); return true;
}
#endif

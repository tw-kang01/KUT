#include "UI/SKHUTutorPanel.h"
#include "KHUTutorModule.h"
#include "AI/KHUProjectMemory.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto SessionFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    bool ContainsSessionLabel(const TSharedRef<SWidget>& Widget, const FString& Text)
    {
        if (Widget->GetType() == FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) { return true; }
        FChildren* Children = Widget->GetChildren();
        for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
        { if (ContainsSessionLabel(Children->GetChildAt(Index), Text)) { return true; } }
        return false;
    }
    class FPendingSessionChat final : public IKHUChatClient
    {
    public:
        int32 Requests = 0;
        virtual void Send(FKHUChatRequest, FCallback) override { ++Requests; }
        virtual void Cancel() override {}
        virtual bool IsMock() const override { return true; }
    };
    class FSessionTools final : public IKHUToolExecutor
    {
    public:
        virtual void Execute(const FString&, const TSharedRef<FJsonObject>&, FCallback Callback) override { Callback(FKHUMcpResult()); }
        virtual void Cancel() override {}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionNavigation, "KHUTutor.UI.Sessions.DraftsActiveSelectionAndBusyGuard", SessionFlags)
bool FSessionNavigation::RunTest(const FString&)
{
    auto Panel = SNew(SKHUTutorPanel);
    auto Config = IKHUTutorModule::Get().PanelConfig(); Config.Context.bProjectMemory = true;
    const FString File = FPaths::ProjectSavedDir() / TEXT("KHUTutor/SessionTests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
    Panel->ProjectMemory = MakeShared<FKHUProjectMemory>(File, TEXT("SESSION_TEST_PROJECT"), Config.Context);

    // Typing before the first send used to store an unreachable Drafts[""] entry.
    Panel->Input->SetText(FText::FromString(TEXT("처음 열어 작성하던 질문")));
    Panel->NewConversation(TEXT("")); const FString Second = Panel->ActiveConversation;
    FString First;
    for (const auto& Draft : Panel->Drafts) { if (Draft.Value == TEXT("처음 열어 작성하던 질문")) { First = Draft.Key; } }
    TestFalse(TEXT("Initial unsent draft receives a reachable id"), First.IsEmpty());
    TestTrue(TEXT("New chat has a distinct id"), !Second.IsEmpty() && Second != First);
    TestTrue(TEXT("New chat composer starts empty"), Panel->Input->GetText().IsEmpty());
    TestFalse(TEXT("No anonymous unreachable draft remains"), Panel->Drafts.Contains(TEXT("")));
    TestEqual(TEXT("Opening a new chat uses chat page"), Panel->Pages->GetActiveWidgetIndex(), 0);

    Panel->Input->SetText(FText::FromString(TEXT("두 번째 채팅의 질문")));
    Panel->NewConversation(TEXT("")); const FString Third = Panel->ActiveConversation;
    Panel->OpenConversation(First);
    TestEqual(TEXT("Initial draft restored"), Panel->Input->GetText().ToString(), FString(TEXT("처음 열어 작성하던 질문")));
    Panel->OpenConversation(Second);
    TestEqual(TEXT("Second draft restored independently"), Panel->Input->GetText().ToString(), FString(TEXT("두 번째 채팅의 질문")));
    Panel->OpenConversation(Third);
    TestTrue(TEXT("Third chat stays empty"), Panel->Input->GetText().IsEmpty());
    TestTrue(TEXT("Sidebar exists alongside chat"), Panel->SessionList.IsValid());
    if (Panel->SessionList)
    {
        TestTrue(TEXT("Sidebar marks the active chat"), ContainsSessionLabel(Panel->SessionList.ToSharedRef(), TEXT("현재 채팅")));
        TestTrue(TEXT("Sidebar identifies unsent drafts"), ContainsSessionLabel(Panel->SessionList.ToSharedRef(), TEXT("작성 중")));
    }

    // Re-selecting the current row must not reset a running request or its draft.
    auto Chat = MakeShared<FPendingSessionChat>();
    Panel->Loop = MakeShared<FKHUAgentLoop>(Config, Chat, MakeShared<FSessionTools>(), MakeShared<FKHUToolBridge>(Config, FKHUToolExecutorDirect::Catalog()));
    Panel->Loop->SetProjectMemory(Panel->ProjectMemory, TEXT(""));
    TestTrue(TEXT("Deferred request starts without network"), Panel->Loop->Ask(TEXT("pending"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, Third));
    const auto Running = Panel->Loop;
    TestTrue(TEXT("Deferred response is busy"), Panel->Busy());
    Panel->Input->SetText(FText::FromString(TEXT("다음 질문 초안")));
    Panel->OpenConversation(Third);
    TestTrue(TEXT("Current selection retains running loop"), Panel->Loop == Running);
    TestEqual(TEXT("Current selection retains composer"), Panel->Input->GetText().ToString(), FString(TEXT("다음 질문 초안")));
    Panel->ShowPage(2);
    Panel->OpenConversation(First);
    TestEqual(TEXT("Blocked navigation reveals chat notice and stop control"), Panel->ActivePage, 0);
    TestEqual(TEXT("Busy request prevents switching to another chat"), Panel->ActiveConversation, Third);
    Panel->NewConversation(TEXT(""));
    TestEqual(TEXT("Busy request prevents creating another chat"), Panel->ActiveConversation, Third);
    TestTrue(TEXT("Blocked navigation explains how to proceed"), Panel->Status.Contains(TEXT("응답을 멈춰")));
    TestEqual(TEXT("Navigation did not send extra requests"), Chat->Requests, 1);
    TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations;
    TestTrue(TEXT("Three conversations remain readable"), Panel->ProjectMemory->ListConversations(Folders, Conversations));
    TestEqual(TEXT("Busy navigation creates no hidden conversation"), Conversations.Num(), 3);
    Panel->Stop(); Panel->ProjectMemory->Forget(); return true;
}
#endif

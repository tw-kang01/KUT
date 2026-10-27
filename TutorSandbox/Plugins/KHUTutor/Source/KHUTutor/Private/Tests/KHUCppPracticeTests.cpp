#include "AI/KHUCppPractice.h"
#include "AI/KHUCppPracticeSnippets.h"
#include "UI/SKHUTutorPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Mcp/KHUMcpProtocol.h"
#include "KHUTutorModule.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto PracticeFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    namespace Given { KHU_CPP_FLOW KHU_CPP_REVIEW }
    // Developer-only reference implementations. No answer keys are attached to student requests.
    bool CanOpenChanged(bool Key, bool Admin, int32 Energy) { return (Key || Admin) && Energy >= 5; }
    bool TryFirst(const TArray<int32>& Values, int32& Out)
    { if (Values.IsEmpty()) { return false; } Out = Values[0]; return true; }
    int32 FirstInRange(const TArray<int32>& D, int32 Limit, int32& Visits)
    {
        Visits = 0;
        for (int32 I = 0; I < D.Num(); ++I) { ++Visits; if (D[I] >= 0 && D[I] <= Limit) { return I; } }
        return INDEX_NONE;
    }
    int32 Nearest(const TArray<int32>& D, int32 Limit, int32& Visits)
    {
        Visits = 0; int32 Best = INDEX_NONE;
        for (int32 I = 0; I < D.Num(); ++I)
        { ++Visits; if (D[I] >= 0 && D[I] <= Limit && (Best == INDEX_NONE || D[I] < D[Best])) { Best = I; } }
        return Best;
    }
    struct FResource
    {
        int32 Value = 10;
        bool Consume(int32 Amount) { if (Amount <= 0 || Amount > Value) { return false; } Value -= Amount; return true; }
        bool Recover(int32 Amount) { if (Amount <= 0) { return false; } Value += FMath::Min(Amount, 10 - Value); return true; }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticeCatalog, "KHUTutor.Practice.CatalogAndDrafts", PracticeFlags)
bool FCppPracticeCatalog::RunTest(const FString&)
{
    TSet<FString> Ids;
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const auto& E : FKHUCppPractice::All())
    {
        TestFalse(TEXT("Stable id unique"), Ids.Contains(E.Id)); Ids.Add(E.Id);
        TestTrue(TEXT("Complete assignment contract"), !E.Goal.IsEmpty() && !E.Problem.IsEmpty() && !E.StudentWork.IsEmpty() && !E.Verification.IsEmpty() && !E.Transfer.IsEmpty());
        const FString Base = FKHUCppPractice::Draft(E.Id), Transfer = FKHUCppPractice::Draft(E.Id, true);
        TestNotEqual(TEXT("Transfer changes the task"), Base, Transfer);
        TestFalse(TEXT("Base does not prematurely introduce transfer"), Base.Contains(E.Transfer));
        TestTrue(TEXT("Transfer is self contained"), Transfer.Contains(E.Problem) && Transfer.Contains(E.Transfer));
        TestTrue(TEXT("Fits normal question boundary"), Base.Len() < 16000 && Transfer.Len() < 16000);
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), E.Id); O->SetStringField(TEXT("base"), Base); O->SetStringField(TEXT("transfer"), Transfer);
        Items.Add(MakeShared<FJsonValueObject>(O));
    }
    TestEqual(TEXT("Six subjects"), Ids.Num(), 6);
    TestTrue(TEXT("Unknown id fails closed"), FKHUCppPractice::Draft(TEXT("../review")).IsEmpty());
    auto Root = MakeShared<FJsonObject>(); Root->SetArrayField(TEXT("exercises"), Items);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("KHUTutor/PracticeTests");
    IFileManager::Get().MakeDirectory(*Directory, true);
    TestTrue(TEXT("Exact product drafts exported for semantic review"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Root), *(Directory / TEXT("catalog.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticePanel, "KHUTutor.Practice.PanelDraftGuards", PracticeFlags)
bool FCppPracticePanel::RunTest(const FString&)
{
    auto Panel = SNew(SKHUTutorPanel);
    Panel->Hint = 0;
    const int32 Before = Panel->MessageCount;
    Panel->DraftPractice(TEXT("review"), false);
    TestEqual(TEXT("Exact draft visible before sending"), Panel->Input->GetText().ToString(), FKHUCppPractice::Draft(TEXT("review")));
    TestEqual(TEXT("Does not raise hint"), Panel->Hint, 0);
    TestEqual(TEXT("Does not send or mark a turn"), Panel->MessageCount, Before);
    TestFalse(TEXT("No task is started by selection"), Panel->bHasTask);
    const FString Draft = Panel->Input->GetText().ToString();
    Panel->DraftPractice(TEXT("flow"), true);
    TestEqual(TEXT("Unsent student text preserved"), Panel->Input->GetText().ToString(), Draft);
    Panel->Input->SetText(FText::GetEmpty()); Panel->bConversationReadOnly = true;
    Panel->DraftPractice(TEXT("flow"), false);
    TestTrue(TEXT("Read-only conversation rejects drafting"), Panel->Input->GetText().IsEmpty());
    Panel->bConversationReadOnly = false;
    Panel->DraftPractice(TEXT("unknown"), false);
    TestTrue(TEXT("Invalid selection cannot replace input"), Panel->Input->GetText().IsEmpty());
    Panel->DraftPractice(TEXT("flow"), true);
    TestEqual(TEXT("Transfer reachable without completing base"), Panel->Input->GetText().ToString(), FKHUCppPractice::Draft(TEXT("flow"), true));
    Panel->Stop(); Panel->Input->SetText(FText::GetEmpty()); Panel->DraftPractice(TEXT("flow"), false);
    TestTrue(TEXT("Stopped panel rejects changes"), Panel->Input->GetText().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticeFlowReview, "KHUTutor.Practice.Reference.FlowAndReview", PracticeFlags)
bool FCppPracticeFlowReview::RunTest(const FString&)
{
    TestTrue(TEXT("Given exact threshold"), Given::CanOpen(true, 3));
    TestFalse(TEXT("Given below threshold"), Given::CanOpen(true, 2));
    TestFalse(TEXT("Given no key"), Given::CanOpen(false, 3));
    for (bool Key : {false, true}) { for (bool Admin : {false, true}) { for (int32 Energy : {0, 4, 5, 6, MAX_int32})
    { TestEqual(TEXT("Changed truth table"), CanOpenChanged(Key, Admin, Energy), Energy >= 5 && (Key || Admin)); } } }
    TestEqual(TEXT("Given nonempty case"), Given::First({7, 9}), 7);
    // The given empty access is deliberately NOT executed: its precondition is false.
    const TArray<int32> Empty;
    TestFalse(TEXT("Counterexample has no valid index zero"), Empty.IsValidIndex(0));
    int32 Out = 91;
    TestFalse(TEXT("Repaired empty returns failure"), TryFirst(Empty, Out));
    TestEqual(TEXT("Failure output contract leaves sentinel unchanged"), Out, 91);
    TestTrue(TEXT("Real zero is distinguishable from failure"), TryFirst({0}, Out));
    TestEqual(TEXT("Zero value preserved"), Out, 0);
    TestTrue(TEXT("Negative first element allowed"), TryFirst({-7, 3}, Out));
    TestEqual(TEXT("Negative value preserved"), Out, -7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticeContainers, "KHUTutor.Practice.Reference.Containers", PracticeFlags)
bool FCppPracticeContainers::RunTest(const FString&)
{
    TSet<int32> Owned;
    TestFalse(TEXT("Empty inventory"), Owned.Contains(4));
    for (int32 Id : {4, 7, 4, 0, -1}) { if (Id > 0) { Owned.Add(Id); } }
    TestEqual(TEXT("Deduplicates and rejects invalid ids"), Owned.Num(), 2);
    TestTrue(TEXT("Contains valid items"), Owned.Contains(4) && Owned.Contains(7));
    TestFalse(TEXT("Missing item"), Owned.Contains(9));
    TMap<int32, int32> Counts;
    const auto Gain = [&Counts](int32 Id) { if (Id > 0) { int32& Count = Counts.FindOrAdd(Id); if (Count < MAX_int32) { ++Count; } } };
    const auto Drop = [&Counts](int32 Id) { if (int32* Count = Counts.Find(Id)) { if (*Count > 0) { --*Count; } } };
    Gain(4); Gain(4); Gain(0); Gain(-1);
    TestEqual(TEXT("Transfer preserves quantities"), Counts.FindRef(4), 2);
    Drop(4); Drop(4); Drop(4); Drop(99);
    TestEqual(TEXT("Cannot become negative"), Counts.FindRef(4), 0);
    TestEqual(TEXT("Dropping missing and invalid gain do not add entries"), Counts.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticeSearch, "KHUTutor.Practice.Reference.Search", PracticeFlags)
bool FCppPracticeSearch::RunTest(const FString&)
{
    const TArray<TArray<int32>> Cases = {{}, {-1}, {0}, {4}, {5}, {7, 4}, {4, 0, 0}, {-2, 7, 0}, {MAX_int32}};
    for (const auto& D : Cases) { for (int32 Limit : {0, 4, MAX_int32})
    {
        TArray<int32> Eligible;
        for (int32 I = 0; I < D.Num(); ++I) { if (D[I] >= 0 && D[I] <= Limit) { Eligible.Add(I); } }
        int32 Visits = 0;
        const int32 First = FirstInRange(D, Limit, Visits);
        TestEqual(TEXT("First matches eligible order"), First, Eligible.IsEmpty() ? INDEX_NONE : Eligible[0]);
        TestEqual(TEXT("Early exit operation count"), Visits, First == INDEX_NONE ? D.Num() : First + 1);
        Eligible.Sort([&D](int32 A, int32 B) { return D[A] == D[B] ? A < B : D[A] < D[B]; });
        TestEqual(TEXT("Nearest and stable tie match independent sorted oracle"), Nearest(D, Limit, Visits), Eligible.IsEmpty() ? INDEX_NONE : Eligible[0]);
        TestEqual(TEXT("Transfer scans whole unsorted input"), Visits, D.Num());
    } }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticeComposition, "KHUTutor.Practice.Reference.CompositionState", PracticeFlags)
bool FCppPracticeComposition::RunTest(const FString&)
{
    FResource Chest, Enemy;
    TestTrue(TEXT("Valid consumption"), Chest.Consume(3)); TestEqual(TEXT("Decreased"), Chest.Value, 7);
    TestEqual(TEXT("Second instance independent"), Enemy.Value, 10);
    for (int32 Amount : {0, -1, 8, MAX_int32})
    { TestFalse(TEXT("Invalid consumption rejected"), Chest.Consume(Amount)); TestEqual(TEXT("Failed operation leaves state"), Chest.Value, 7); }
    TestTrue(TEXT("Exact amount"), Chest.Consume(7)); TestEqual(TEXT("Zero remaining"), Chest.Value, 0);
    TestFalse(TEXT("Cannot consume below zero"), Chest.Consume(1));
    TestTrue(TEXT("Recovery"), Chest.Recover(3)); TestEqual(TEXT("Recovered"), Chest.Value, 3);
    TestTrue(TEXT("Huge recovery saturates without signed overflow"), Chest.Recover(MAX_int32)); TestEqual(TEXT("At capacity"), Chest.Value, 10);
    TestFalse(TEXT("Negative recovery rejected"), Chest.Recover(-1)); TestFalse(TEXT("Zero recovery rejected"), Chest.Recover(0));
    TestEqual(TEXT("Other instance still unchanged"), Enemy.Value, 10);
    // This checks reusable state logic, not attachment, registration, ownership or PIE execution.
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCppPracticeRecallBudget, "KHUTutor.Practice.OptionalRecallPreservesRequiredContext", PracticeFlags)
bool FCppPracticeRecallBudget::RunTest(const FString&)
{
    class FRecordingChat final : public IKHUChatClient
    {
    public:
        TArray<FKHUChatRequest> Requests;
        virtual void Send(FKHUChatRequest R, FCallback Callback) override
        {
            Requests.Add(R); FKHUChatResponse A; A.bMock = A.bSuccess = true; A.Content = TEXT("연습을 이어갑니다.");
            A.Assistant = MakeShared<FJsonObject>(); A.Assistant->SetStringField(TEXT("role"), TEXT("assistant")); A.Assistant->SetStringField(TEXT("content"), A.Content);
            Callback(MoveTemp(A));
        }
        virtual bool IsMock() const override { return true; }
        virtual bool OwnsHistory() const override { return true; }
        virtual void Cancel() override {}
    };
    auto C = IKHUTutorModule::Get().PanelConfig();
    const FString Path = FPaths::ProjectSavedDir() / TEXT("KHUTutor/PracticeTests") / (FGuid::NewGuid().ToString() + TEXT(".json"));
    auto Memory = MakeShared<FKHUProjectMemory>(Path, TEXT("E2_SYNTHETIC_PROJECT"), C.Context);
    const auto MakeLoop = [&Memory](const FKHUConfig& Settings, TSharedRef<FRecordingChat> Chat)
    {
        auto L = MakeShared<FKHUAgentLoop>(Settings, Chat, MakeShared<FKHUToolExecutorDirect>(), MakeShared<FKHUToolBridge>(Settings, FKHUToolExecutorDirect::Catalog()));
        L->SetProjectMemory(Memory, TEXT("")); L->SetEditorObservation(TEXT("CURRENT_OBSERVATION_SENTINEL")); return L;
    };
    auto BaselineChat = MakeShared<FRecordingChat>(); auto Baseline = MakeLoop(C, BaselineChat);
    const FString Question = FKHUCppPractice::Draft(TEXT("review"), true);
    Baseline->Ask(Question, TEXT("T1"), 0, [](EKHUAgentState, const FString&) {});
    if (!TestEqual(TEXT("Baseline request sent"), BaselineChat->Requests.Num(), 1)) { Memory->Forget(); return false; }
    C.Tiers[TEXT("T1")].InputBudget = BaselineChat->Requests[0].EstimatedInputTokens + 200;
    for (const TCHAR* Kind : {TEXT("goal"), TEXT("decision"), TEXT("attempt"), TEXT("open"), TEXT("next")})
    { TestTrue(TEXT("Optional historical note saved"), Memory->Remember(TEXT(""), Kind, TEXT("OPTIONAL_NOTE_SENTINEL ") + FString::ChrN(1000, TCHAR(0xAC00)), 0)); }
    auto Chat = MakeShared<FRecordingChat>(); auto Agent = MakeLoop(C, Chat);
    Agent->Ask(Question, TEXT("T1"), 0, [](EKHUAgentState, const FString&) {});
    if (TestEqual(TEXT("Optional recall cannot prevent a fitting question"), Chat->Requests.Num(), 1))
    {
        const auto& R = Chat->Requests[0]; FString User;
        for (const auto& V : R.Messages) { if (V->AsObject()->GetStringField(TEXT("role")) == TEXT("user")) { User = V->AsObject()->GetStringField(TEXT("content")); } }
        TestTrue(TEXT("Full question remains"), User.Contains(Question));
        TestTrue(TEXT("Current observation remains"), User.Contains(TEXT("CURRENT_OBSERVATION_SENTINEL")));
        TestTrue(TEXT("Omission explicitly marked"), User.Contains(TEXT("PROJECT_PROGRESS_OMITTED")));
        TestFalse(TEXT("Optional history excluded"), User.Contains(TEXT("OPTIONAL_NOTE_SENTINEL")));
        TestEqual(TEXT("Hint unchanged"), R.Hint, 0);
        TestTrue(TEXT("Still fits configured token limit"), R.EstimatedInputTokens <= C.Tiers[TEXT("T1")].InputBudget);
        TestTrue(TEXT("Receipt records omission"), Agent->Receipt()->GetArrayField(TEXT("context"))[0]->AsObject()->GetBoolField(TEXT("retrieved_memory_omitted")));
    }
    C.Tiers[TEXT("T1")].InputBudget = 1;
    auto TooSmall = MakeShared<FRecordingChat>(); auto Blocked = MakeLoop(C, TooSmall);
    Blocked->Ask(Question, TEXT("T1"), 0, [](EKHUAgentState, const FString&) {});
    TestEqual(TEXT("Required context still fails closed"), TooSmall->Requests.Num(), 0);
    Baseline->Cancel(); Agent->Cancel(); Blocked->Cancel(); Memory->Forget(); return true;
}
#endif

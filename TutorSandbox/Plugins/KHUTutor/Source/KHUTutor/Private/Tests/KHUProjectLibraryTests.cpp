#include "AI/KHUProjectMemory.h"
#include "AI/KHUAgentLoop.h"
#include "Mcp/KHUMcpProtocol.h"
#include "KHUTutorModule.h"
#include "UI/SKHUTutorPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FString File() { return FPaths::ProjectSavedDir() / TEXT("KHUTutor/LibraryTests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")); }
    FKHUConfig Config() { auto C = IKHUTutorModule::Get().PanelConfig(); C.Context.bProjectMemory = true; return C; }
    class FChat final : public IKHUChatClient
    {
    public:
        TArray<FKHUChatRequest> Requests;
        virtual void Send(FKHUChatRequest R, FCallback Callback) override
        { Requests.Add(R); FKHUChatResponse A; A.bSuccess = A.bMock = true; A.Content = TEXT("MOCK_REPLY"); A.Assistant = MakeShared<FJsonObject>(); A.Assistant->SetStringField(TEXT("role"), TEXT("assistant")); A.Assistant->SetStringField(TEXT("content"), A.Content); Callback(MoveTemp(A)); }
        virtual void Cancel() override {}
        virtual bool IsMock() const override { return true; }
    };
    class FTools final : public IKHUToolExecutor
    { public: virtual void Execute(const FString&, const TSharedRef<FJsonObject>&, FCallback C) override { C(FKHUMcpResult()); } virtual void Cancel() override {} };
    TSharedRef<FKHUAgentLoop> Loop(const FKHUConfig& C, const TSharedRef<FChat>& Chat)
    { return MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FTools>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog())); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryFolders, "KHUTutor.Library.FoldersRenamePersist", Flags)
bool FLibraryFolders::RunTest(const FString&)
{
    const FString Path = File(); auto C = Config(); FKHUProjectMemory M(Path, TEXT("P"), C.Context); FString Folder, Chat;
    TestTrue(TEXT("Create folder"), M.CreateFolder(TEXT("문 만들기"), Folder)); TestTrue(TEXT("Create empty conversation"), M.CreateConversation(TEXT("/Game/Door"), Folder, 1, Chat));
    TestTrue(TEXT("Rename folder"), M.EditFolder(Folder, TEXT("Door와 스위치"), false)); TestTrue(TEXT("Rename conversation"), M.EditConversation(Chat, TEXT("DoorTarget 연결"), Folder, false));
    FKHUProjectMemory Reopened(Path, TEXT("P"), C.Context); TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations;
    TestTrue(TEXT("Restart loads library"), Reopened.ListConversations(Folders, Conversations)); TestEqual(TEXT("Folder count"), Folders.Num(), 1); TestEqual(TEXT("Conversation count"), Conversations.Num(), 1);
    if (Folders.Num() == 1 && Conversations.Num() == 1) { TestEqual(TEXT("Stable folder id"), Folders[0].Id, Folder); TestEqual(TEXT("Renamed title"), Conversations[0].Title, FString(TEXT("DoorTarget 연결"))); TestEqual(TEXT("Scope remains attached"), Conversations[0].Scope, FString(TEXT("/Game/Door"))); }
    TestFalse(TEXT("Oversized title rejected"), Reopened.EditFolder(Folder, FString::ChrN(81, 'x'), false)); M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryArchive, "KHUTutor.Library.MoveArchiveRestore", Flags)
bool FLibraryArchive::RunTest(const FString&)
{
    auto C = Config(); FKHUProjectMemory M(File(), TEXT("P"), C.Context); FString A, B, Id;
    M.CreateFolder(TEXT("A"), A); M.CreateFolder(TEXT("B"), B); M.CreateConversation(TEXT(""), A, 1, Id); M.RecordTurn(Id, TEXT(""), TEXT("goal"), TEXT("user"), TEXT("FULL_ORIGINAL"), TEXT("u1"), 1, 1);
    TestTrue(TEXT("Move conversation"), M.EditConversation(Id, TEXT("moved"), B, false)); TestTrue(TEXT("Archive folder"), M.EditFolder(B, TEXT("B"), true)); FKHUSavedConversation Saved;
    M.ReadConversation(Id, 1, Saved); TestTrue(TEXT("Parent archive blocks resume"), Saved.bArchived && Saved.bFolderArchived && !Saved.bOwnArchived);
    TestEqual(TEXT("Original survives archive"), Saved.Turns.Num(), 1); TestTrue(TEXT("Restore folder"), M.EditFolder(B, TEXT("B"), false));
    M.ReadConversation(Id, 1, Saved); TestFalse(TEXT("Restored folder restores child visibility"), Saved.bArchived);
    M.EditConversation(Id, TEXT("moved"), B, true); M.ReadConversation(Id, 1, Saved); TestTrue(TEXT("Individual archive"), Saved.bOwnArchived);
    M.EditConversation(Id, TEXT("moved"), B, false); M.ReadConversation(Id, 1, Saved); TestEqual(TEXT("Restore retains folder"), Saved.Folder, B); TestEqual(TEXT("Restore retains text"), Saved.Turns[0]->AsObject()->GetStringField(TEXT("text")), FString(TEXT("FULL_ORIGINAL")));
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryLegacy, "KHUTutor.Library.LegacyAndHintReads", Flags)
bool FLibraryLegacy::RunTest(const FString&)
{
    auto C = Config(); FKHUProjectMemory M(File(), TEXT("P"), C.Context);
    M.SaveTask(TEXT("legacy"), TEXT(""), TEXT("old goal"), TEXT("old observation"), TEXT("old suggestion"), 1, 1);
    M.RecordTurn(TEXT("old"), TEXT(""), TEXT("old chat"), TEXT("user"), TEXT("VISIBLE"), TEXT("u1"), 1, 1);
    M.RecordTurn(TEXT("old"), TEXT(""), TEXT("old chat"), TEXT("assistant"), TEXT("HIDDEN_DETAIL"), TEXT("a1"), 3, 1);
    TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations; TestTrue(TEXT("Existing archive requires no deletion or migration"), M.ListConversations(Folders, Conversations)); TestEqual(TEXT("Old conversations in default folder"), Conversations.Num(), 2);
    FKHUSavedConversation Saved; M.ReadConversation(TEXT("old"), 1, Saved); TestEqual(TEXT("Higher hint hidden"), Saved.HiddenReplies, 1); TestEqual(TEXT("Student text visible"), Saved.Turns.Num(), 1);
    M.ReadConversation(TEXT("old"), 3, Saved); TestEqual(TEXT("Higher hint can read retained answer"), Saved.Turns.Num(), 2); TestEqual(TEXT("Next sequence never overwrites original"), Saved.NextTurn, 2);
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryCorrupt, "KHUTutor.Library.ForeignAndCorruptNeverOverwrite", Flags)
bool FLibraryCorrupt::RunTest(const FString&)
{
    const FString Path = File(); auto C = Config(); FKHUProjectMemory M(Path, TEXT("P"), C.Context); FString Id, Folder; M.CreateFolder(TEXT("folder"), Folder); M.CreateConversation(TEXT(""), Folder, 1, Id);
    FString Original; FFileHelper::LoadFileToString(Original, *Path); FKHUProjectMemory Foreign(Path, TEXT("OTHER_PROJECT"), C.Context); TArray<FKHULearningFolder> F; TArray<FKHUSavedConversation> Chats;
    TestFalse(TEXT("Different project cannot list"), Foreign.ListConversations(F, Chats)); TestFalse(TEXT("Different project cannot edit"), Foreign.EditFolder(Folder, TEXT("bad"), false));
    TSharedPtr<FJsonObject> Broken; if (!TestTrue(TEXT("Stored JSON parses"), KHUMcpProtocol::ReadObject(Original, Broken))) { M.Forget(); return false; }
    auto Library = Broken->GetObjectField(TEXT("library")); Library->GetArrayField(TEXT("conversations"))[0]->AsObject()->SetStringField(TEXT("folder"), TEXT("missing"));
    const FString Corrupt = KHUMcpProtocol::WriteObject(Broken.ToSharedRef()); FFileHelper::SaveStringToFile(Corrupt, *Path); FKHUProjectMemory Reopened(Path, TEXT("P"), C.Context);
    TestFalse(TEXT("Dangling folder rejected"), Reopened.ListConversations(F, Chats)); TestFalse(TEXT("Corrupt metadata not overwritten"), Reopened.CreateFolder(TEXT("replacement"), Folder)); FString After; FFileHelper::LoadFileToString(After, *Path); TestEqual(TEXT("Corrupt source retained"), After, Corrupt);
    FFileHelper::SaveStringToFile(Original, *Path); M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryRecall, "KHUTutor.Library.SelectedFolderRecall", Flags)
bool FLibraryRecall::RunTest(const FString&)
{
    auto C = Config(); FKHUProjectMemory M(File(), TEXT("P"), C.Context); FString A, B, One, Two; M.CreateFolder(TEXT("Door"), A); M.CreateFolder(TEXT("Move"), B);
    M.CreateConversation(TEXT(""), A, 1, One); M.CreateConversation(TEXT(""), B, 1, Two);
    M.RecordTurn(One, TEXT(""), TEXT("DOOR_GROUP_GOAL"), TEXT("user"), TEXT("DOOR_GROUP_HISTORY"), TEXT("u1"), 1, 1);
    M.RecordTurn(Two, TEXT(""), TEXT("OTHER_GROUP_GOAL"), TEXT("user"), TEXT("OTHER_GROUP_HISTORY"), TEXT("u1"), 1, 1);
    FString Recall = M.Recall(TEXT(""), 1, TEXT("이어서"), One); TestTrue(TEXT("Selected folder recalled"), Recall.Contains(TEXT("DOOR_GROUP_HISTORY"))); TestFalse(TEXT("Other folder history excluded"), Recall.Contains(TEXT("OTHER_GROUP_HISTORY"))); TestFalse(TEXT("Other folder summary excluded"), Recall.Contains(TEXT("OTHER_GROUP_GOAL")));
    M.EditConversation(Two, TEXT("moved"), A, false); Recall = M.Recall(TEXT(""), 1, TEXT("OTHER_GROUP_HISTORY"), One); TestTrue(TEXT("Move changes retrieval grouping"), Recall.Contains(TEXT("OTHER_GROUP_HISTORY"))); M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryResume, "KHUTutor.Library.ResumeConversationProtocol", Flags)
bool FLibraryResume::RunTest(const FString&)
{
    auto C = Config(); auto M = MakeShared<FKHUProjectMemory>(File(), TEXT("P"), C.Context); FString Id; M->CreateConversation(TEXT("/Game/Door"), TEXT(""), 1, Id);
    M->RecordTurn(Id, TEXT("/Game/Door"), TEXT("ORIGINAL_GOAL"), TEXT("user"), TEXT("DoorTarget = Door A HISTORICAL\n\nEDITOR_OBSERVATION_JSON OLD_LIVE"), TEXT("u1"), 1, 1);
    M->RecordTurn(Id, TEXT("/Game/Door"), TEXT("ORIGINAL_GOAL"), TEXT("assistant"), TEXT("HIDDEN_HIGH_HINT"), TEXT("a1"), 3, 1);
    auto Chat = MakeShared<FChat>(); auto Agent = Loop(C, Chat); Agent->SetProjectMemory(M, TEXT("/Game/Door")); Agent->SetEditorObservation(TEXT("FRESH_ONLY"));
    TestTrue(TEXT("Reopen same conversation"), Agent->Ask(TEXT("계속할게"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, Id));
    TestEqual(TEXT("One mock request"), Chat->Requests.Num(), 1); TestEqual(TEXT("Conversation identity retained"), Agent->Receipt()->GetStringField(TEXT("task_id")), Id);
    if (Chat->Requests.Num() == 1) { const FString Wire = KHUResponsesProtocol::Body(Chat->Requests[0]); TestTrue(TEXT("Original remembered"), Wire.Contains(TEXT("HISTORICAL"))); TestTrue(TEXT("Current observation included"), Wire.Contains(TEXT("FRESH_ONLY"))); TestFalse(TEXT("Old observation omitted"), Wire.Contains(TEXT("OLD_LIVE"))); TestFalse(TEXT("Higher hint not replayed"), Wire.Contains(TEXT("HIDDEN_HIGH_HINT"))); TestTrue(TEXT("Restored protocol valid"), FKHUContextBudgeter::Validate(Chat->Requests[0].Messages)); }
    FKHUSavedConversation Saved; M->ReadConversation(Id, 3, Saved); TestEqual(TEXT("Both old and new messages kept"), Saved.Turns.Num(), 4); TestEqual(TEXT("Next user id advances"), Saved.NextTurn, 3);
    auto Other = Loop(C, Chat); Other->SetProjectMemory(M, TEXT("/Game/Other")); TestFalse(TEXT("Cannot resume into another asset"), Other->Ask(TEXT("wrong scope"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, Id));
    Agent->Cancel(); M->EditConversation(Id, TEXT("archived"), TEXT(""), true); auto Archived = Loop(C, Chat); Archived->SetProjectMemory(M, TEXT("/Game/Door")); TestFalse(TEXT("Archived conversation does not send"), Archived->Ask(TEXT("wrong archive"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, Id)); TestEqual(TEXT("Rejected resumes made no requests"), Chat->Requests.Num(), 1); M->Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLibraryBudget, "KHUTutor.Library.ResumeRetainsBudget", Flags)
bool FLibraryBudget::RunTest(const FString&)
{
    auto C = Config(); C.PerTaskCredits = 12; auto M = MakeShared<FKHUProjectMemory>(File(), TEXT("P"), C.Context); FString Id; M->CreateConversation(TEXT(""), TEXT(""), 1, Id);
    M->SaveConversationBudget(Id, 10, 11, true); auto Chat = MakeShared<FChat>(); auto Agent = Loop(C, Chat); Agent->SetProjectMemory(M, TEXT(""));
    Agent->Ask(TEXT("continue"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, Id);
    TestEqual(TEXT("Request uses restored remaining credits"), Chat->Requests.Num(), 1);
    if (Chat->Requests.Num() == 1) { TestTrue(TEXT("Reopening never grants fresh task credits"), FMath::IsNearlyEqual(Chat->Requests[0].RemainingCredits, 1.0)); }
    FKHUSavedConversation Saved; M->ReadConversation(Id, 1, Saved); TestTrue(TEXT("Budget retained"), Saved.bHasBudget && FMath::IsNearlyEqual(Saved.Allowance, 11.0) && FMath::IsNearlyEqual(Saved.Spent, 10.0)); Agent->Cancel(); M->Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectManagerFlow, "KHUTutor.UI.Projects.ManageAndReadOnlyScope", Flags)
bool FProjectManagerFlow::RunTest(const FString&)
{
    auto Panel = SNew(SKHUTutorPanel); auto C = Config(); Panel->ProjectMemory = MakeShared<FKHUProjectMemory>(File(), TEXT("P"), C.Context);
    Panel->EditProjectName(TEXT("new_folder"), TEXT(""), TEXT("")); Panel->ProjectNameInput->SetText(FText::FromString(TEXT("문 만들기"))); Panel->SaveProjectName(); const FString Folder = Panel->ActiveFolder;
    TestFalse(TEXT("Folder created through form"), Folder.IsEmpty()); Panel->NewConversation(Folder); const FString Id = Panel->ActiveConversation; TestFalse(TEXT("Conversation created through UI"), Id.IsEmpty()); TestEqual(TEXT("Creation opens chat"), Panel->Pages->GetActiveWidgetIndex(), 0);
    Panel->Input->SetText(FText::FromString(TEXT("작성 중인 초안"))); Panel->NewConversation(Folder); const FString Next = Panel->ActiveConversation; Panel->OpenConversation(Id); TestEqual(TEXT("Conversation switch keeps its draft"), Panel->Input->GetText().ToString(), FString(TEXT("작성 중인 초안")));
    FString Foreign; Panel->ProjectMemory->CreateConversation(TEXT("/Game/Other"), Folder, 1, Foreign); Panel->OpenConversation(Foreign); TestTrue(TEXT("Other asset is read only"), Panel->bConversationReadOnly); Panel->Submit(TEXT("do not send")); TestFalse(TEXT("No loop for wrong asset"), Panel->Loop.IsValid());
    Panel->OpenConversation(Next); auto Chat = MakeShared<FChat>(); Panel->Loop = Loop(C, Chat); Panel->Loop->SetProjectMemory(Panel->ProjectMemory, TEXT("")); const TWeakPtr<SKHUTutorPanel> Weak = Panel;
    Panel->Loop->Ask(TEXT("current task"), TEXT("T1"), 1, [Weak](EKHUAgentState State, const FString& Text) { if (auto P = Weak.Pin()) { P->OnEvent(State, Text); } }, false, true, Next); Panel->bHasTask = true;
    Panel->ProjectMemory->EditConversation(Next, TEXT("archived current"), Folder, true); Panel->RefreshProjects();
    TestFalse(TEXT("Archiving current task cancels its loop without reentrancy"), Panel->Loop.IsValid()); TestTrue(TEXT("Archived current chat is read only"), Panel->bConversationReadOnly); TestEqual(TEXT("Cancellation does not duplicate folder rows"), Panel->ProjectList->GetChildren()->Num(), 2);
    Panel->OpenConversation(Id); Panel->bApiConnected = false; Panel->Input->SetText(FText::FromString(TEXT("not authenticated"))); Panel->Submit(Panel->Input->GetText().ToString()); TestEqual(TEXT("Authentication still required"), Panel->Pages->GetActiveWidgetIndex(), 2); TestFalse(TEXT("Opening a conversation never makes paid request"), Panel->Loop.IsValid());
    Panel->Stop(); Panel->ProjectMemory->Forget(); return true;
}
#endif

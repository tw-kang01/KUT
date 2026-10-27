#include "AI/KHUPortfolio.h"
#include "AI/KHUProjectMemory.h"
#include "KHUTutorModule.h"
#include "UI/SKHUTutorPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FString File() { return FPaths::ProjectSavedDir() / TEXT("KHUTutor/PortfolioTests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")); }
    FKHUContextPolicy Policy() { auto C = IKHUTutorModule::Get().PanelConfig().Context; C.bProjectMemory = true; return C; }
    void Seed(FKHUProjectMemory& M, const FString& Scope = TEXT(""))
    {
        M.RecordTurn(TEXT("case"), Scope, TEXT("문제"), TEXT("user"), TEXT("가설 A로 시도했지만 실패. 아직 실행 미확인."), TEXT("u1"), 1, 1);
        M.RecordTurn(TEXT("case"), Scope, TEXT("문제"), TEXT("assistant"), TEXT("HIGH_HINT_CODE_SENTINEL"), TEXT("a1"), 3, 1);
    }
    FKHUPortfolio Draft(FKHUProjectMemory& M, int32 Hint = 1)
    { FKHUSavedConversation C; M.ReadConversation(TEXT("case"), Hint, C); return FKHUPortfolio::Draft(C, {TEXT("u1")}, Hint); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioDraft, "KHUTutor.Portfolio.EvidenceAndNoInventedResults", Flags)
bool FPortfolioDraft::RunTest(const FString&)
{
    FKHUProjectMemory M(File(), TEXT("P"), Policy()); Seed(M); FKHUSavedConversation C; M.ReadConversation(TEXT("case"), 1, C);
    auto R = FKHUPortfolio::Draft(C, {TEXT("u1"), TEXT("a1"), TEXT("missing")}, 1);
    TestEqual(TEXT("Only selected visible originals"), R.Sources.Num(), 1);
    TestTrue(TEXT("Failure retained as reported in selected AI context"), R.Prompt(C).Contains(TEXT("시도했지만 실패")));
    TestTrue(TEXT("Writing scaffold stays compact"), R.Body.Len() < 600);
    TestFalse(TEXT("Source excerpts do not crowd the editable body"), R.Body.Contains(TEXT("[u1]")) || R.Body.Contains(TEXT("가설 A로 시도했지만 실패")));
    TestTrue(TEXT("Unmeasured stays unmeasured"), R.Body.Contains(TEXT("미측정")));
    TestFalse(TEXT("No high hint answer"), R.Body.Contains(TEXT("HIGH_HINT_CODE_SENTINEL")));
    TestFalse(TEXT("Scaffold is not confirmed"), R.bConfirmed);
    TestTrue(TEXT("Well formed draft"), FKHUPortfolio::Valid(R.Json()));
    R.Body += TEXT("\nAuthorization: Bearer sk-portfolio-secret-1234567890\nC:\\Users\\PrivateStudent\\secret.cpp");
    const FString Safe = R.Json()->GetStringField(TEXT("body"));
    TestFalse(TEXT("Credential removed"), Safe.Contains(TEXT("sk-portfolio-secret-1234567890")));
    TestFalse(TEXT("Personal path removed"), Safe.Contains(TEXT("PrivateStudent")));
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioPersist, "KHUTutor.Portfolio.RestartRevisionAndOriginals", Flags)
bool FPortfolioPersist::RunTest(const FString&)
{
    const FString Path = File(); FKHUProjectMemory M(Path, TEXT("P"), Policy()); Seed(M); auto R = Draft(M);
    R.Body = TEXT("실패한 진행 중 사례. 실행 미확인."); R.Evidence = TEXT("공개 테스트 요약: 미실행"); R.bConfirmed = true;
    TestTrue(TEXT("Save"), M.SavePortfolio(TEXT("case"), TEXT(""), 1, R)); auto Stale = R;
    FKHUProjectMemory Reopen(Path, TEXT("P"), Policy()); FKHUSavedConversation C;
    TestTrue(TEXT("Restart reads"), Reopen.ReadConversation(TEXT("case"), 1, C));
    if (TestTrue(TEXT("Record present"), C.Portfolio.IsValid()))
    { auto Saved = FKHUPortfolio::Read(C.Portfolio); TestEqual(TEXT("Body retained"), Saved.Body, R.Body); TestTrue(TEXT("Confirmed revision retained"), Saved.bConfirmed); }
    R.Body += TEXT("\n추가 시도도 미확인"); R.bConfirmed = false;
    TestTrue(TEXT("Revision update"), Reopen.SavePortfolio(TEXT("case"), TEXT(""), 1, R));
    TestFalse(TEXT("Other panel cannot overwrite"), M.SavePortfolio(TEXT("case"), TEXT(""), 1, Stale));
    Reopen.ReadConversation(TEXT("case"), 3, C); TestEqual(TEXT("Originals unchanged"), C.Turns.Num(), 2);
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioBoundary, "KHUTutor.Portfolio.HintScopeFolderAndProject", Flags)
bool FPortfolioBoundary::RunTest(const FString&)
{
    const FString Path = File(); FKHUProjectMemory M(Path, TEXT("P"), Policy()); Seed(M, TEXT("/Game/A")); auto R = Draft(M, 3);
    TestFalse(TEXT("Other asset blocked"), M.SavePortfolio(TEXT("case"), TEXT("/Game/B"), 3, R));
    TestTrue(TEXT("Exact asset"), M.SavePortfolio(TEXT("case"), TEXT("/Game/A"), 3, R)); FKHUSavedConversation C;
    M.ReadConversation(TEXT("case"), 1, C); TestTrue(TEXT("Higher hint draft hidden"), C.bPortfolioHidden && !C.Portfolio);
    auto Low = Draft(M, 1); TestFalse(TEXT("Hidden record cannot be overwritten"), M.SavePortfolio(TEXT("case"), TEXT("/Game/A"), 1, Low));
    FKHUProjectMemory Foreign(Path, TEXT("OTHER"), Policy()); TestFalse(TEXT("Other project blocked"), Foreign.SavePortfolio(TEXT("case"), TEXT("/Game/A"), 3, R));
    FString Folder; M.CreateFolder(TEXT("learning"), Folder); M.EditConversation(TEXT("case"), TEXT("case"), Folder, false); M.EditFolder(Folder, TEXT("learning"), true);
    TestFalse(TEXT("Archived folder blocked"), M.SavePortfolio(TEXT("case"), TEXT("/Game/A"), 3, R));
    M.EditFolder(Folder, TEXT("learning"), false); R.Sources.Add(TEXT("missing"));
    TestFalse(TEXT("Foreign reference blocked"), M.SavePortfolio(TEXT("case"), TEXT("/Game/A"), 3, R));
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioCorrupt, "KHUTutor.Portfolio.CorruptLegacyAndSaveFailure", Flags)
bool FPortfolioCorrupt::RunTest(const FString&)
{
    const FString Path = File(); FKHUProjectMemory M(Path, TEXT("P"), Policy()); Seed(M); FKHUSavedConversation C;
    TestTrue(TEXT("Old archive with no portfolio remains readable"), M.ReadConversation(TEXT("case"), 1, C)); TestFalse(TEXT("No invented old record"), C.Portfolio.IsValid());
    auto R = Draft(M); M.SavePortfolio(TEXT("case"), TEXT(""), 1, R);
    FString Original; FFileHelper::LoadFileToString(Original, *Path); TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Original, O);
    O->GetArrayField(TEXT("archive"))[0]->AsObject()->GetObjectField(TEXT("portfolio"))->SetNumberField(TEXT("hint"), 99);
    const FString Corrupt = KHUMcpProtocol::WriteObject(O.ToSharedRef()); FFileHelper::SaveStringToFile(Corrupt, *Path);
    FKHUProjectMemory Reopened(Path, TEXT("P"), Policy());
    TestFalse(TEXT("Corrupt read fails"), Reopened.ReadConversation(TEXT("case"), 1, C));
    TestFalse(TEXT("Corrupt source never overwritten"), Reopened.SavePortfolio(TEXT("case"), TEXT(""), 1, R));
    FString After; FFileHelper::LoadFileToString(After, *Path); TestEqual(TEXT("Original corrupt bytes retained"), After, Corrupt);
    FFileHelper::SaveStringToFile(Original, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    auto Tiny = Policy(); FTCHARToUTF8 OriginalBytes(*Original); Tiny.ArchiveMaxBytes = OriginalBytes.Length() + 100;
    FKHUProjectMemory Limited(Path, TEXT("P"), Tiny); const FString Revision = R.Revision;
    TestTrue(TEXT("Archive fits before attempted write"), Limited.ReadConversation(TEXT("case"), 1, C));
    R.Body = FString::ChrN(15000, 'x');
    TestFalse(TEXT("Storage failure returned"), Limited.SavePortfolio(TEXT("case"), TEXT(""), 1, R));
    TestEqual(TEXT("Failed save doesn't advance revision"), R.Revision, Revision);
    FFileHelper::LoadFileToString(After, *Path); TestEqual(TEXT("Failure preserves disk"), After, Original);
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioPanel, "KHUTutor.Portfolio.PanelEditConfirmExportAndPrompt", Flags)
bool FPortfolioPanel::RunTest(const FString&)
{
    auto M = MakeShared<FKHUProjectMemory>(File(), TEXT("P"), Policy()); Seed(*M);
    auto P = SNew(SKHUTutorPanel); P->bMock = false; P->Hint = 1; P->ProjectMemory = M; P->OpenPortfolio(TEXT("case"));
    TestEqual(TEXT("Record page opens"), P->ActivePage, 3);
    TestEqual(TEXT("New record begins with source selection"), P->PortfolioStep, 0);
    P->GeneratePortfolio(); TestTrue(TEXT("No draft generated without a source"), P->PortfolioBody->GetText().IsEmpty());
    P->PortfolioSelection = {TEXT("u1")}; P->GeneratePortfolio();
    TestEqual(TEXT("Draft advances to writing"), P->PortfolioStep, 1);
    FString Copy; TestFalse(TEXT("Unconfirmed cannot export"), P->PortfolioExport(Copy));
    P->PortfolioRecord.bConfirmed = true;
    P->PortfolioBody->SetText(FText::FromString(TEXT("학생이 수정한 미해결 사례")));
    P->GeneratePortfolio();
    TestEqual(TEXT("Generating again never overwrites authored text"), P->PortfolioBody->GetText().ToString(), FString(TEXT("학생이 수정한 미해결 사례")));
    TestFalse(TEXT("Editing invalidates confirmation"), P->PortfolioRecord.bConfirmed);
    P->PortfolioEvidence->SetText(FText::FromString(TEXT("공개 요약: 실행 미확인"))); P->PortfolioRecord.bConfirmed = true;
    TestTrue(TEXT("Confirmed content can export"), P->PortfolioExport(Copy)); TestTrue(TEXT("Edited body exported"), Copy.Contains(TEXT("학생이 수정한")));
    TestFalse(TEXT("Selected raw conversations are not included in publication copy"), Copy.Contains(TEXT("가설 A로 시도했지만 실패")) || Copy.Contains(TEXT("[u1]")));
    P->OpenPortfolio(TEXT("case")); TestEqual(TEXT("Saved record reopens at writing"), P->PortfolioStep, 1);
    TestTrue(TEXT("Reopened record retains confirmation"), P->PortfolioRecord.bConfirmed);
    P->Hint = 0; TestFalse(TEXT("Hint drop blocks export"), P->PortfolioExport(Copy)); TestTrue(TEXT("No hidden copy content"), Copy.IsEmpty()); P->Hint = 1;
    P->Input->SetText(FText::FromString(TEXT("작성 중 질문"))); P->PreparePortfolioPrompt(); TestEqual(TEXT("Input not overwritten"), P->Input->GetText().ToString(), FString(TEXT("작성 중 질문")));
    P->Input->SetText(FText::GetEmpty()); P->PreparePortfolioPrompt();
    TestEqual(TEXT("Composer contains a readable request"), P->Input->GetText().ToString(), FKHUPortfolio::Question());
    TestFalse(TEXT("Composer contains no internal source envelope"), P->Input->GetText().ToString().Contains(TEXT("<record_data>")));
    TestTrue(TEXT("Selected record context is attached separately"), P->RecordDrafts.Contains(P->ActiveConversation)
        && P->RecordDrafts.FindRef(P->ActiveConversation).Context.Contains(TEXT("<record_data>"))
        && P->RecordDrafts.FindRef(P->ActiveConversation).Context.Contains(TEXT("가설 A로 시도했지만 실패")));
    TestFalse(TEXT("No paid call"), P->Loop.IsValid());
    P->Input->SetText(FText::GetEmpty()); P->Drafts.Add(P->ActiveConversation, TEXT("STALE_DRAFT_ALREADY_SENT")); P->PreparePortfolioPrompt();
    TestEqual(TEXT("Cleared active composer overrides stale cached draft"), P->Input->GetText().ToString(), FKHUPortfolio::Question());
    const FString RecordConversation = P->ActiveConversation;
    const FString AttachedContext = P->RecordDrafts.FindRef(RecordConversation).Context;
    P->NewConversation(TEXT(""));
    TestTrue(TEXT("Another chat opens without inheriting the attachment"), P->ActiveConversation != RecordConversation
        && !P->RecordDrafts.Contains(P->ActiveConversation));
    P->Input->SetText(FText::FromString(TEXT("다른 채팅의 질문"))); P->OpenConversation(RecordConversation);
    TestEqual(TEXT("Returning restores the readable question"), P->Input->GetText().ToString(), FKHUPortfolio::Question());
    TestEqual(TEXT("Returning preserves the same record attachment"), P->RecordDrafts.FindRef(RecordConversation).Context, AttachedContext);
    const FString EditedQuestion = TEXT("선택한 방법의 이유를 먼저 설명해 줘.");
    P->Input->SetText(FText::FromString(EditedQuestion)); P->PreparePortfolioPrompt();
    TestEqual(TEXT("Refreshing an attachment preserves the student's edited question"), P->Input->GetText().ToString(), EditedQuestion);
    TestTrue(TEXT("Refreshing retains a separate context attachment"), P->RecordDrafts.Contains(RecordConversation));
    P->RecordDrafts.Remove(RecordConversation);
    TestEqual(TEXT("Removing an attachment preserves the visible question"), P->Input->GetText().ToString(), EditedQuestion);
    P->Input->SetText(FText::FromString(EditedQuestion + TEXT(" 실패한 시도도 포함해 줘.")));
    TestEqual(TEXT("Question remains editable without an attachment"), P->Input->GetText().ToString(), EditedQuestion + TEXT(" 실패한 시도도 포함해 줘."));
    P->Input->SetText(FText::GetEmpty()); P->PreparePortfolioPrompt();

    class FNoPaidClient final : public IKHUChatClient
    {
    public:
        int32 Requests = 0;
        virtual void Send(FKHUChatRequest, FCallback Callback) override
        {
            ++Requests; FKHUChatResponse Response; Response.bSuccess = Response.bMock = true; Response.Content = TEXT("검증용 응답");
            Response.Assistant = MakeShared<FJsonObject>(); Response.Assistant->SetStringField(TEXT("role"), TEXT("assistant"));
            Response.Assistant->SetStringField(TEXT("content"), Response.Content); Callback(MoveTemp(Response));
        }
        virtual void Cancel() override {}
        virtual bool IsMock() const override { return true; }
    };
    auto Client = MakeShared<FNoPaidClient>(); auto Config = IKHUTutorModule::Get().PanelConfig(); Config.Context.bProjectMemory = true;
    P->Loop = MakeShared<FKHUAgentLoop>(Config, Client, MakeShared<FKHUToolExecutorDirect>(), MakeShared<FKHUToolBridge>(Config, FKHUToolExecutorDirect::Catalog()));
    P->Loop->SetProjectMemory(M, TEXT("")); P->bApiConnected = true;
    P->ChangeHint(0, false); P->Submit(P->Input->GetText().ToString(), false);
    TestEqual(TEXT("Changed hint blocks the stale attached request before sending"), Client->Requests, 0);
    TestEqual(TEXT("Blocked submission preserves the editable question"), P->Input->GetText().ToString(), FKHUPortfolio::Question());
    P->Input->SetText(FText::GetEmpty()); P->PreparePortfolioPrompt();
    TestTrue(TEXT("A lower hint cannot re-stage a higher-hint record"), P->Input->GetText().IsEmpty());
    P->ChangeHint(2, false); P->OpenPortfolio(RecordConversation); P->PreparePortfolioPrompt();
    TestEqual(TEXT("Reopening and preparing binds the attachment to the current hint"), P->RecordDrafts.FindRef(RecordConversation).Hint, 2);
    TestEqual(TEXT("Reprepared request is still readable"), P->Input->GetText().ToString(), FKHUPortfolio::Question());
    TestEqual(TEXT("Attachment lifecycle makes no model request"), Client->Requests, 0);
    Config.Tiers[TEXT("T1")].InputBudget = 1;
    P->Loop = MakeShared<FKHUAgentLoop>(Config, Client, MakeShared<FKHUToolExecutorDirect>(), MakeShared<FKHUToolBridge>(Config, FKHUToolExecutorDirect::Catalog()));
    P->Loop->SetProjectMemory(M, TEXT("")); P->bHasTask = false;
    const FString RequiredContext = P->RecordDrafts.FindRef(RecordConversation).Context;
    P->Submit(P->Input->GetText().ToString(), false);
    TestEqual(TEXT("Over-budget attachment is rejected without any model request"), Client->Requests, 0);
    TestTrue(TEXT("Rejected attachment reports the context limit"), P->Loop->Receipt()->GetStringField(TEXT("text")).Contains(TEXT("문맥 한도")));
    TestEqual(TEXT("Budget rejection preserves the editable question"), P->Input->GetText().ToString(), FKHUPortfolio::Question());
    TestTrue(TEXT("Budget rejection keeps the complete attachment for retry"), P->RecordDrafts.Contains(RecordConversation)
        && P->RecordDrafts.FindRef(RecordConversation).Context == RequiredContext);
    P->Stop(); M->Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioPrompt, "KHUTutor.Portfolio.PromptPreservesFactsAndStudentEdits", Flags)
bool FPortfolioPrompt::RunTest(const FString&)
{
    FKHUProjectMemory M(File(), TEXT("P"), Policy()); Seed(M); FKHUSavedConversation C; M.ReadConversation(TEXT("case"), 1, C);
    auto R = FKHUPortfolio::Draft(C, {TEXT("u1")}, 1); const FString Original = R.Body;
    const FString Prompt = R.Prompt(C);
    TestFalse(TEXT("Untouched scaffold questions are not sent as authored facts"), Prompt.Contains(TEXT("어떤 상황에서 무엇을 해결하려 했나요?")));
    TestTrue(TEXT("Selected reported failure and uncertainty retained"), Prompt.Contains(TEXT("가설 A로 시도했지만 실패. 아직 실행 미확인.")));
    TestTrue(TEXT("Role, source id and time retained"), Prompt.Contains(TEXT("[u1] 학생 발언·보고")) && Prompt.Contains(C.Turns[0]->AsObject()->GetStringField(TEXT("updated_utc"))));
    TestFalse(TEXT("Higher-hint answer stays hidden"), Prompt.Contains(TEXT("HIGH_HINT_CODE_SENTINEL")));
    TestEqual(TEXT("Preparing request does not edit saved body"), R.Body, Original);
    TestFalse(TEXT("Untouched evidence guidance is not sent as observed results"), Prompt.Contains(TEXT("공개할 코드나 테스트 요약을 적으세요")));
    R.Evidence = TEXT("Play에서 숫자 2를 눌렀을 때 Guard 선택 표시가 바뀜. 이동은 아직 미확인.");
    TestTrue(TEXT("Separate authored verification results reach AI request"), R.Prompt(C).Contains(R.Evidence));
    TestTrue(TEXT("Verification results are labeled as student authored"), R.Prompt(C).Contains(TEXT("학생이 작성한 확인 방법과 결과")));
    R.Body = TEXT("내 설명: 판단을 바꾸었지만 실행은 아직 미확인.\n") + R.Body;
    TestTrue(TEXT("Any authored edit preserves the complete body"), R.Prompt(C).Contains(R.Body, ESearchCase::CaseSensitive));
    R.Body = FString::ChrN(15000, TCHAR(0xAC00)) + TEXT("END_OF_AUTHORED_FACTS");
    TestTrue(TEXT("Long authored material is never silently cut"), R.Prompt(C).Contains(R.Body, ESearchCase::CaseSensitive));
    M.RecordTurn(TEXT("case"), TEXT(""), TEXT("문제"), TEXT("user"), TEXT("두 번째 시도의 판단도 미확인입니다."), TEXT("u2"), 1, 1);
    M.ReadConversation(TEXT("case"), 1, C);
    auto Reordered = FKHUPortfolio::Draft(C, {TEXT("u2"), TEXT("u1")}, 1);
    Reordered.Sources = {TEXT("u2"), TEXT("u1")};
    TestFalse(TEXT("Checkbox selection order does not defeat scaffold de-duplication"), Reordered.Prompt(C).Contains(TEXT("어떤 상황에서 무엇을 해결하려 했나요?")));
    Reordered.Body = TEXT("기존 저장 기록\n선택한 원문 발췌 (과거 기록, 현재 상태의 증거가 아님)\n[u1] 기존 학생 설명과 직접 수정한 문장");
    TestTrue(TEXT("Legacy authored records remain intact in requests"), Reordered.Prompt(C).Contains(Reordered.Body, ESearchCase::CaseSensitive));
    TestTrue(TEXT("Newly selected messages still accompany legacy records"), Reordered.Prompt(C).Contains(TEXT("두 번째 시도의 판단도 미확인입니다.")));
    M.Forget(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioContextRegression, "KHUTutor.Portfolio.BlockedBlindRequestFitsOriginalBudget", Flags)
bool FPortfolioContextRegression::RunTest(const FString&)
{
    FString Text; TSharedPtr<FJsonObject> Original;
    const FString Fixtures = FPaths::ProjectDir() / TEXT("../fixtures/blind_e2e3/20261018");
    if (!TestTrue(TEXT("Load actual blocked blind receipt"), FFileHelper::LoadFileToString(Text, *(Fixtures / TEXT("case_1_turn_3.json"))) && KHUMcpProtocol::ReadObject(Text, Original))) { return false; }
    TestTrue(TEXT("Original request really stopped before a paid call"), Original->GetArrayField(TEXT("usage")).IsEmpty() && Original->GetStringField(TEXT("text")).Contains(TEXT("문맥 한도")));
    if (!TestTrue(TEXT("Load recorded original turns"), FFileHelper::LoadFileToString(Text, *(Fixtures / TEXT("case_1_memory.json"))) && KHUMcpProtocol::ReadObject(Text, Original))) { return false; }
    const auto Recorded = Original->GetArrayField(TEXT("archive"))[0]->AsObject();
    TArray<FString> Selected; for (const auto& V : Recorded->GetObjectField(TEXT("portfolio"))->GetArrayField(TEXT("sources"))) { Selected.Add(V->AsString()); }
    auto C = IKHUTutorModule::Get().PanelConfig(); C.Context.bProjectMemory = true;
    const int32 Limit = C.Tiers[TEXT("T1")].InputBudget;
    auto M = MakeShared<FKHUProjectMemory>(File(), TEXT("P"), C.Context);
    for (const auto& V : Recorded->GetArrayField(TEXT("turns")))
    {
        const auto T = V->AsObject(); if (!Selected.Contains(T->GetStringField(TEXT("id")))) { continue; }
        TestTrue(TEXT("Original selected turn copied into isolated memory"), M->RecordTurn(TEXT("case"), TEXT(""), TEXT("blind replay"), T->GetStringField(TEXT("role")), T->GetStringField(TEXT("text")), T->GetStringField(TEXT("id")), 2, 1));
    }
    FKHUSavedConversation Conversation; M->ReadConversation(TEXT("case"), 2, Conversation);
    const auto Record = FKHUPortfolio::Draft(Conversation, Selected, 2);
    const FString Question = FKHUPortfolio::Question(), RecordContext = Record.Prompt(Conversation);
    for (const auto& V : Conversation.Turns)
    { TestTrue(TEXT("Every selected original excerpt remains"), RecordContext.Contains(FKHUProjectMemory::Excerpt(V->AsObject()->GetStringField(TEXT("text")), 420), ESearchCase::CaseSensitive)); }
    class FRecording final : public IKHUChatClient
    {
    public:
        TArray<FKHUChatRequest> Requests;
        virtual void Send(FKHUChatRequest Request, FCallback Callback) override
        {
            Requests.Add(Request); FKHUChatResponse Response; Response.bSuccess = Response.bMock = true; Response.Content = TEXT("검증용 응답");
            Response.Assistant = MakeShared<FJsonObject>(); Response.Assistant->SetStringField(TEXT("role"), TEXT("assistant")); Response.Assistant->SetStringField(TEXT("content"), Response.Content); Callback(MoveTemp(Response));
        }
        virtual void Cancel() override {}
        virtual bool IsMock() const override { return true; }
        virtual bool OwnsHistory() const override { return true; }
    };
    auto Chat = MakeShared<FRecording>();
    auto Agent = MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FKHUToolExecutorDirect>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
    Agent->SetProjectMemory(M, TEXT(""));
    TestTrue(TEXT("Replayed retrospective accepted"), Agent->Ask(Question, TEXT("T1"), 2, [](EKHUAgentState, const FString&) {}, false, false, TEXT("case"), RecordContext));
    if (TestEqual(TEXT("Previously blocked source selection reaches the client"), Chat->Requests.Num(), 1))
    {
        const auto& Request = Chat->Requests[0];
        TestTrue(TEXT("Unchanged input budget fits"), Request.EstimatedInputTokens <= Limit);
        TestEqual(TEXT("No hint escalation"), Request.Hint, 2);
        TestTrue(TEXT("Record request carries no irrelevant editor tool definitions"), Request.Tools.IsEmpty() && Request.SerializedTools.IsEmpty());
        auto Evidence = MakeShared<FJsonObject>(); Evidence->SetNumberField(TEXT("estimated_input_tokens"), Request.EstimatedInputTokens);
        Evidence->SetNumberField(TEXT("input_budget"), Limit); Evidence->SetNumberField(TEXT("selected_sources"), Selected.Num());
        Evidence->SetStringField(TEXT("question"), Question);
        Evidence->SetStringField(TEXT("record_context"), RecordContext);
        FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Evidence), *(FPaths::ProjectSavedDir() / TEXT("KHUTutor/e3_context_l0_regression.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    Agent->Cancel(); M->Forget(); return true;
}
#endif

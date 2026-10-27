#include "AI/KHULearningCoach.h"
#include "AI/KHUHintPolicy.h"
#include "UI/SKHUTutorPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "AI/KHUAgentLoop.h"
#include "AI/KHUClaudeRuntime.h"
#include "AI/KHUSecretStore.h"
#include "KHUTutorModule.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedRef<FJsonObject> Json(const FString& Text)
    { TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Text, O); return O.ToSharedRef(); }
    FString File() { return FPaths::ProjectSavedDir() / TEXT("KHUTutor/CoachTests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")); }
    FKHUConfig Config() { auto C = IKHUTutorModule::Get().PanelConfig(); C.Context.bProjectMemory = true; return C; }
    TSharedRef<FJsonObject> Review(const FString& Quote = TEXT("문이 안 열려요"), const FString& Source = TEXT("u1"))
    {
        auto O = Json(TEXT("{\"stage\":\"define\",\"action\":\"verify\",\"trigger\":\"none\",\"report\":\"none\",\"source\":\"u1\",\"quote\":\"x\",\"items\":[{\"item\":\"observation\",\"status\":\"supported\",\"source\":\"u1\",\"quote\":\"x\"}]}"));
        O->SetStringField(TEXT("source"), Source); O->SetStringField(TEXT("quote"), Quote);
        const auto I = O->GetArrayField(TEXT("items"))[0]->AsObject(); I->SetStringField(TEXT("source"), Source); I->SetStringField(TEXT("quote"), Quote); return O;
    }
    FString Raw(const TSharedRef<FJsonObject>& O) { return TEXT("도움을 먼저 드려요.\n<khu_review>") + KHUMcpProtocol::WriteObject(O) + TEXT("</khu_review>"); }
    TArray<FKHUCoachSource> Sources() { return {{TEXT("u1"), TEXT("문이 안 열려요"), TEXT("student"), true}}; }
    class FChat : public IKHUChatClient
    {
    public:
        FString Text = Raw(Review()); bool Deferred = false; FCallback Pending; TArray<FKHUChatRequest> Requests;
        virtual void Send(FKHUChatRequest R, FCallback C) override { Requests.Add(R); if (Deferred) { Pending = MoveTemp(C); } else { Finish(C); } }
        void Finish(FCallback C) { FKHUChatResponse R; R.bMock = R.bSuccess = true; R.Content = Text; R.Assistant = MakeShared<FJsonObject>(); R.Assistant->SetStringField(TEXT("role"), TEXT("assistant")); R.Assistant->SetStringField(TEXT("content"), Text); C(MoveTemp(R)); }
        virtual void Cancel() override {}
        virtual bool IsMock() const override { return true; }
    };
    /** Real product runtime (Claude Code → ChatKHU → private MCP) with the raw reply and control captured for fixtures. */
    class FRecorder final : public IKHUChatClient
    {
    public:
        TSharedRef<FKHUClaudeRuntime> Runtime; FString RawText, Control;
        FRecorder(FKHUConfig C, FString K, TSharedPtr<FKHUProjectMemory> M) : Runtime(MakeShared<FKHUClaudeRuntime>(C, MoveTemp(K), M)) {}
        virtual void Send(FKHUChatRequest R, FCallback Callback) override
        { Control = R.CoachControl; Runtime->Send(MoveTemp(R), [this, Callback = MoveTemp(Callback)](FKHUChatResponse Response) mutable { RawText = Response.Content; Callback(MoveTemp(Response)); }); }
        virtual void Cancel() override { Runtime->Cancel(); }
        virtual bool IsMock() const override { return false; }
        virtual bool OwnsHistory() const override { return true; }
        virtual void ObserveTools(FToolObserver Observer) override { Runtime->ObserveTools(MoveTemp(Observer)); }
    };
    TSharedRef<FKHUAgentLoop> Loop(const FKHUConfig& C, const TSharedRef<IKHUChatClient>& Chat)
    { return MakeShared<FKHUAgentLoop>(C, Chat, MakeShared<FKHUToolExecutorDirect>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog())); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachContract, "KHUTutor.Coach.ContractAndMalformedTrailer", Flags)
bool FCoachContract::RunTest(const FString&)
{
    FKHULearningCoach C; TestEqual(TEXT("Body preserved"), C.Consume(Raw(Review()), 0, Sources(), TEXT("")), FString(TEXT("도움을 먼저 드려요.")));
    TestTrue(TEXT("Valid review and one safe action"), C.Review.IsValid() && !C.Action(0).IsEmpty());
    for (const FString Bad : {FString(TEXT("정상 본문<khu_review>{bad}</khu_review>")), FString(TEXT("정상 본문<khu_review>{unfinished")), FString(TEXT("정상 본문<khu_review>"))})
    { TestEqual(TEXT("Malformed data never displayed"), C.Consume(Bad, 1, Sources(), TEXT("")), FString(TEXT("정상 본문"))); TestFalse(TEXT("No normal diagnosis"), C.Review.IsValid()); }
    auto O = Review(); O->SetStringField(TEXT("action"), TEXT("execute_shell")); C.Consume(Raw(O), 1, Sources(), TEXT("")); TestFalse(TEXT("Unknown action rejected"), C.Review.IsValid());
    O = Review(); O->SetNumberField(TEXT("hint"), 3); C.Consume(Raw(O), 0, Sources(), TEXT("")); TestFalse(TEXT("Model cannot change hint"), C.Review.IsValid());
    const FString Trailer = TEXT("<khu_review>") + KHUMcpProtocol::WriteObject(Review()) + TEXT("</khu_review>");
    TestEqual(TEXT("A fence around metadata leaves with it"), C.Consume(TEXT("도움을 먼저 드려요.\n```json\n") + Trailer + TEXT("\n```"), 1, Sources(), TEXT("")), FString(TEXT("도움을 먼저 드려요.")));
    TestTrue(TEXT("Fenced review is still validated"), C.Review.IsValid());
    TestTrue(TEXT("A real code block keeps its closing fence"), C.Consume(TEXT("예시예요.\n```cpp\nint A = 0;\n```\n") + Trailer, 3, Sources(), TEXT("")).EndsWith(TEXT("```"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachEvidence, "KHUTutor.Coach.MissingAndForeignEvidence", Flags)
bool FCoachEvidence::RunTest(const FString&)
{
    FKHULearningCoach C;
    C.Consume(Raw(Review(TEXT("없는 관찰"))), 1, Sources(), TEXT("")); TestFalse(TEXT("Fabricated quote rejected"), C.Review.IsValid());
    C.Consume(Raw(Review(TEXT("문이 안 열려요"), TEXT("other_asset:u1"))), 1, Sources(), TEXT("")); TestFalse(TEXT("Foreign reference rejected"), C.Review.IsValid());
    auto O = Review(); O->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(TEXT("source"), TEXT("missing")); C.Consume(Raw(O), 1, Sources(), TEXT("")); TestFalse(TEXT("Every item requires evidence"), C.Review.IsValid());
    O = Review(); auto Item = MakeShared<FJsonObject>(); Item->Values = O->GetArrayField(TEXT("items"))[0]->AsObject()->Values; Item->SetStringField(TEXT("item"), TEXT("goal")); Item->SetStringField(TEXT("status"), TEXT("improve")); O->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(TEXT("status"), TEXT("improve"));
    auto Items = O->GetArrayField(TEXT("items")); Items.Add(MakeShared<FJsonValueObject>(Item)); O->SetArrayField(TEXT("items"), Items); C.Consume(Raw(O), 1, Sources(), TEXT("")); TestFalse(TEXT("At most one improvement"), C.Review.IsValid());
    C.Consume(Raw(Review(TEXT("sk-testsecret123"))), 3, {{TEXT("u1"), TEXT("sk-testsecret123"), TEXT("student"), true}}, TEXT(""));
    TestFalse(TEXT("In-memory receipt masks source secrets before storage"), KHUMcpProtocol::WriteObject(C.Save()).Contains(TEXT("sk-testsecret123"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachDelivery, "KHUTutor.Coach.MissingReviewDeliveryPersistsWithoutDiagnosis", Flags)
bool FCoachDelivery::RunTest(const FString&)
{
    FString Fixture;
    if (!TestTrue(TEXT("Load the actual E3 omitted-review response"), FFileHelper::LoadFileToString(Fixture, *(FPaths::ProjectDir() / TEXT("../fixtures/e3/20261018/live_revision1/real_live_00.json"))))) { return false; }
    const auto Receipt = Json(Fixture); const FString Body = Receipt->GetStringField(TEXT("raw_response"));
    FKHULearningCoach C;
    TestEqual(TEXT("Actual body retained byte-for-character"), C.Consume(Body, 3, {}, TEXT("")), Body);
    TestFalse(TEXT("No invented diagnosis"), C.Review.IsValid()); TestTrue(TEXT("No invented performed action"), C.Events.IsEmpty());
    TestTrue(TEXT("Unavailable review is explicit"), C.Notice(3).Contains(TEXT("정보가 없어요")));
    TestFalse(TEXT("No assignment"), !C.Action(3).IsEmpty()); TestFalse(TEXT("No fabricated review description"), !C.Describe(3).IsEmpty());
    const FString Id = C.Id(); TestFalse(TEXT("Availability gets a stable UI identity"), Id.IsEmpty());
    const FString Path = File(); FKHUProjectMemory M(Path, TEXT("P"), Config().Context); FString Conversation;
    M.CreateConversation(TEXT(""), TEXT(""), 3, Conversation);
    TestTrue(TEXT("Unavailable state persists in existing archive"), M.SaveCoach(Conversation, TEXT(""), C.Save()));
    FKHUProjectMemory Reopened(Path, TEXT("P"), Config().Context); FKHUSavedConversation Saved; Reopened.ReadConversation(Conversation, 3, Saved);
    FKHULearningCoach Restored; TestTrue(TEXT("State reopens"), Restored.Restore(Saved.Coach)); TestEqual(TEXT("UI identity survives restart"), Restored.Id(), Id);
    TestEqual(TEXT("Notice survives restart"), Restored.Notice(3), C.Notice(3)); TestFalse(TEXT("Hint drop hides previous status"), Restored.Notice(0).Contains(TEXT("정보가 없어요")));
    TestEqual(TEXT("Malformed trailer keeps body"), Restored.Consume(TEXT("본문\n<khu_review>{bad}</khu_review>"), 1, {}, TEXT("")), FString(TEXT("본문")));
    TestTrue(TEXT("Malformed distinct from absent"), Restored.Notice(1).Contains(TEXT("확인하지 못했어요")));
    auto Forged = Restored.Save(); Forged->GetObjectField(TEXT("delivery"))->SetStringField(TEXT("state"), TEXT("mastered"));
    TestFalse(TEXT("Availability cannot grant mastery"), FKHULearningCoach::Valid(Forged));
    TestTrue(TEXT("Legacy empty state still opens"), Restored.Restore(Json(TEXT("{\"version\":1,\"paused\":false,\"events\":[]}"))));
    TestTrue(TEXT("Legacy absent status remains empty"), Restored.Id().IsEmpty() && Restored.Notice(1).IsEmpty());
    M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachBodyAction, "KHUTutor.Coach.BodyActionDoesNotAddSecondAssignment", Flags)
bool FCoachBodyAction::RunTest(const FString&)
{
    FString Fixture;
    if (!TestTrue(TEXT("Load actual E2 duplicated-coaching response"), FFileHelper::LoadFileToString(Fixture, *(FPaths::ProjectDir() / TEXT("../fixtures/e2/20261018/initial/real_live_01.json"))))) { return false; }
    const auto Receipt = Json(Fixture); const FString RawText = Receipt->GetStringField(TEXT("raw_response"));
    const FString Student = Receipt->GetStringField(TEXT("fixture_question")); FKHULearningCoach C;
    TestEqual(TEXT("Real normal answer is not deleted"), C.Consume(RawText, 1, {{TEXT("u2"), Student, TEXT("student"), true}}, TEXT("")), Receipt->GetStringField(TEXT("text")));
    TestTrue(TEXT("Valid review retained"), C.Review.IsValid()); TestTrue(TEXT("No duplicate card assignment"), C.Action(1).IsEmpty());
    TestTrue(TEXT("No second proposed action recorded"), C.Events.IsEmpty());
    auto O = Review(); O->SetStringField(TEXT("action"), TEXT("verify"));
    FKHULearningCoach Normal; Normal.Consume(TEXT("읽어 온 오류의 의미입니다.\n<khu_review>") + KHUMcpProtocol::WriteObject(O) + TEXT("</khu_review>"), 1, Sources(), TEXT(""));
    TestFalse(TEXT("Answer without assignment keeps the one optional action"), Normal.Action(1).IsEmpty());
    FKHULearningCoach Quoted; Quoted.Consume(TEXT("예시 문자열입니다.\n```cpp\nTEXT(\"예상해 보세요\")\n```\n> 비교해 보세요\n<khu_review>") + KHUMcpProtocol::WriteObject(O) + TEXT("</khu_review>"), 1, Sources(), TEXT(""));
    TestFalse(TEXT("Code and blockquote are not assignments"), Quoted.Action(1).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachL0Question, "KHUTutor.Coach.L0QuestionDoesNotCreateSecondAction", Flags)
bool FCoachL0Question::RunTest(const FString&)
{
    const FString Question = TEXT("아직 확인하지 않은 결과는 무엇인가요?");
    const FString Trailer = TEXT("\n<khu_review>") + KHUMcpProtocol::WriteObject(Review()) + TEXT("</khu_review>");
    FKHULearningCoach C; TestEqual(TEXT("One visible question retained exactly"), C.Consume(Question + Trailer, 0, Sources(), TEXT("")), Question);
    TestTrue(TEXT("Diagnosis remains valid"), C.Review.IsValid());
    TestTrue(TEXT("No second L0 action in the card or event history"), C.Action(0).IsEmpty() && C.Events.IsEmpty());
    FKHULearningCoach Wide; Wide.Consume(TEXT("아직 확인하지 않은 결과는 무엇인가요？") + Trailer, 0, Sources(), TEXT(""));
    TestTrue(TEXT("Full-width question mark also suppresses extra action"), Wide.Action(0).IsEmpty());
    FKHULearningCoach Higher; Higher.Consume(Question + Trailer, 1, Sources(), TEXT(""));
    TestFalse(TEXT("Other hint levels keep their existing action behavior"), Higher.Action(1).IsEmpty());
    const FString Note = FKHUHintPolicy::SystemNote(0, TEXT("T1"));
    TestTrue(TEXT("Task question-only and concept direct-answer rules coexist"), Note.Contains(TEXT("exactly one short Korean")) && Note.Contains(TEXT("Simple concept/menu")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachSourceReference, "KHUTutor.Coach.CurrentQuestionIsReferencedWithoutEcho", Flags)
bool FCoachSourceReference::RunTest(const FString&)
{
    const FString Question = TEXT("문이 안 열려요\nUNIQUE_CURRENT_STUDENT_73629");
    auto Chat = MakeShared<FChat>(); auto Agent = Loop(Config(), Chat);
    TestTrue(TEXT("Question starts"), Agent->Ask(Question, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    if (TestEqual(TEXT("One captured model request"), Chat->Requests.Num(), 1))
    {
        FString User;
        for (const auto& V : Chat->Requests[0].Messages) { if (V->AsObject()->GetStringField(TEXT("role")) == TEXT("user")) { User = V->AsObject()->GetStringField(TEXT("content")); } }
        TestTrue(TEXT("Full current question preserved"), User.StartsWith(Question));
        TestEqual(TEXT("Current source is not copied a second time"), User.Find(TEXT("UNIQUE_CURRENT_STUDENT_73629"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Question.Len()), INDEX_NONE);
        TestTrue(TEXT("Source id explicitly refers to the visible question"), User.Contains(TEXT("current_question_above")) && User.Contains(TEXT("u1")));
        TestTrue(TEXT("Local quote validation still has the complete source"), Agent->LearningCoach().Review.IsValid());
    }
    const FString Follow = TEXT("기록용 원문입니다: ") + Question + TEXT("\n이 과거 발언의 의미를 정리합니다.");
    Agent->FollowUp(Follow, 1);
    if (TestEqual(TEXT("Follow-up captured"), Chat->Requests.Num(), 2))
    {
        FString User;
        for (const auto& V : Chat->Requests[1].Messages) { if (V->AsObject()->GetStringField(TEXT("role")) == TEXT("user")) { User = V->AsObject()->GetStringField(TEXT("content")); } }
        TestEqual(TEXT("An earlier full report quoted in the current question is not echoed again"), User.Find(TEXT("UNIQUE_CURRENT_STUDENT_73629"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Follow.Len()), INDEX_NONE);
    }
    Agent->Cancel(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachChoices, "KHUTutor.Coach.ChoicesRepeatAndPause", Flags)
bool FCoachChoices::RunTest(const FString&)
{
    FKHULearningCoach C; C.Consume(Raw(Review()), 1, Sources(), TEXT("")); TestTrue(TEXT("Skip accepted"), C.Choose(TEXT("skip")));
    TestEqual(TEXT("Click is not performed"), C.Review->GetStringField(TEXT("status")), FString(TEXT("skip")));
    FKHULearningCoach Reopened; TestTrue(TEXT("Restored"), Reopened.Restore(C.Save())); Reopened.Consume(Raw(Review()), 1, Sources(), TEXT("")); TestTrue(TEXT("Skip suppression survives restart"), Reopened.Action(1).IsEmpty());
    auto O = Review(TEXT("빈 입력으로 직접 실행했더니 실패했어요"), TEXT("u2")); O->SetStringField(TEXT("stage"), TEXT("verify")); O->SetStringField(TEXT("trigger"), TEXT("observation")); O->SetStringField(TEXT("report"), TEXT("verification"));
    const TArray<FKHUCoachSource> S = {{TEXT("u2"), TEXT("빈 입력으로 직접 실행했더니 실패했어요"), TEXT("student"), true}};
    Reopened.Consume(Raw(O), 1, S, TEXT("")); TestFalse(TEXT("New observation permits next action"), Reopened.Action(1).IsEmpty());
    TestTrue(TEXT("Report retained without inventing rationale"), Reopened.Events.ContainsByPredicate([](const auto& V) { return V->AsObject()->GetStringField(TEXT("status")) == TEXT("student_report"); }));
    Reopened.Choose(TEXT("pause")); O->SetStringField(TEXT("quote"), TEXT("실패했어요")); Reopened.Consume(Raw(O), 1, S, TEXT("")); TestTrue(TEXT("Paused ordinary help still returned; no action"), Reopened.Action(1).IsEmpty());
    Reopened.Choose(TEXT("resume")); Reopened.Consume(Raw(Review()), 1, Sources(), TEXT("")); TestTrue(TEXT("Resume does not reset old stage"), Reopened.Action(1).IsEmpty()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachNoGrades, "KHUTutor.Coach.NoLengthGradeOrAIContribution", Flags)
bool FCoachNoGrades::RunTest(const FString&)
{
    auto O = Review(TEXT("모르겠어요")); O->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(TEXT("item"), TEXT("thinking"));
    for (const FString Text : {FString(TEXT("모르겠어요")), FString(TEXT("이 상황에 대해서 자세히 설명하자면 아직 잘 모르겠어요"))})
    { FKHULearningCoach C; C.Consume(Raw(O), 0, {{TEXT("u1"), Text, TEXT("student"), true}}, TEXT("")); TestTrue(TEXT("Short and long identical evidence accepted equally"), C.Review.IsValid()); }
    O->SetStringField(TEXT("report"), TEXT("attempt")); O->SetStringField(TEXT("trigger"), TEXT("decision"));
    FKHULearningCoach C; C.Consume(Raw(O), 1, {{TEXT("u1"), TEXT("모르겠어요"), TEXT("student"), true}}, TEXT("AI가 다듬은 문장: 모르겠어요"));
    TestFalse(TEXT("AI rewritten quote is not student work"), C.Events.ContainsByPredicate([](const auto& V) { return V->AsObject()->GetStringField(TEXT("status")) == TEXT("student_report"); }));
    // Replay of the two real Sonnet trailers for the same door problem (fixtures/e1/20261014/live_revision4_cases00_01):
    // the model called only the longer, more formal wording a performed check.
    const FString Short = TEXT("문이 안 열려요. 목표는 버튼을 누르면 열리는 거예요. 실행해 보니 그대로 닫혀 있어요.");
    const FString Long = TEXT("현재 제가 만들려는 기능을 자세하게 설명드리면 버튼을 눌렀을 때 문이 열리는 기능입니다. 실제로 실행해서 확인한 결과 버튼을 눌렀는데도 문이 그대로 닫혀 있었습니다. 이 문제를 도와주세요.");
    const FString ShortTrailer = TEXT("{\"stage\":\"verify\",\"action\":\"none\",\"trigger\":\"none\",\"report\":\"none\",\"source\":\"u1\",\"quote\":\"문이 안 열려요. 목표는 버튼을 누르면 열리는 거예요.\",\"items\":[{\"item\":\"goal\",\"status\":\"supported\",\"source\":\"u1\",\"quote\":\"목표는 버튼을 누르면 열리는 거예요.\"},{\"item\":\"observation\",\"status\":\"supported\",\"source\":\"u1\",\"quote\":\"실행해 보니 그대로 닫혀 있어요.\"}]}");
    const FString LongTrailer = TEXT("{\"stage\":\"verify\",\"action\":\"none\",\"trigger\":\"observation\",\"report\":\"verification\",\"source\":\"u1\",\"quote\":\"실제로 실행해서 확인한 결과 버튼을 눌렀는데도 문이 그대로 닫혀 있었습니다\",\"items\":[{\"item\":\"goal\",\"status\":\"supported\",\"source\":\"u1\",\"quote\":\"버튼을 눌렀을 때 문이 열리는 기능입니다\"},{\"item\":\"observation\",\"status\":\"supported\",\"source\":\"u1\",\"quote\":\"버튼을 눌렀는데도 문이 그대로 닫혀 있었습니다\"}]}");
    auto Headings = [](const FString& Text) { FString Out; TArray<FString> Lines; Text.ParseIntoArrayLines(Lines); for (const auto& Line : Lines) { if (!Line.StartsWith(TEXT(" "))) { Out += Line + TEXT("|"); } } return Out; };
    FKHULearningCoach A, B; A.Consume(TEXT("도움\n<khu_review>") + ShortTrailer + TEXT("</khu_review>"), 1, {{TEXT("u1"), Short, TEXT("student"), true}}, TEXT(""));
    B.Consume(TEXT("도움\n<khu_review>") + LongTrailer + TEXT("</khu_review>"), 1, {{TEXT("u1"), Long, TEXT("student"), true}}, TEXT(""));
    TestTrue(TEXT("Both real reviews accepted"), A.Review.IsValid() && B.Review.IsValid());
    TestEqual(TEXT("Same problem in short or long wording shows the same diagnosis and record"), Headings(B.Describe(1)), Headings(A.Describe(1)));
    TestTrue(TEXT("An opening symptom is observation evidence, not a separate performed check"), B.Events.IsEmpty() && !B.Describe(1).Contains(TEXT("남긴 기록")));
    auto Later = Json(LongTrailer); Later->SetStringField(TEXT("source"), TEXT("u2")); for (const auto& V : Later->GetArrayField(TEXT("items"))) { V->AsObject()->SetStringField(TEXT("source"), TEXT("u2")); }
    FKHULearningCoach Follow; Follow.Consume(Raw(Later), 1, {{TEXT("u2"), Long, TEXT("student"), true}}, TEXT(""));
    TestTrue(TEXT("The same words as a later follow-up result are still recorded"), Follow.Describe(1).Contains(TEXT("남긴 기록 · 직접 확인한 결과"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachHypothetical, "KHUTutor.Coach.HypotheticalIsNotPerformed", Flags)
bool FCoachHypothetical::RunTest(const FString&)
{
    // Replay the real E2 response that incorrectly called an assumed result verification.
    const FString Quote = TEXT("빈 배열에서 false, 원소 0 하나에서 true와 출력 0을 확인했다고 가정합니다.");
    auto O = Review(Quote, TEXT("u4")); O->SetStringField(TEXT("stage"), TEXT("verify"));
    O->SetStringField(TEXT("trigger"), TEXT("decision")); O->SetStringField(TEXT("report"), TEXT("verification"));
    FKHULearningCoach C; C.Consume(Raw(O), 2, {{TEXT("u4"), Quote, TEXT("student"), true}}, TEXT(""));
    TestTrue(TEXT("Ordinary help and review retained"), C.Review.IsValid());
    TestEqual(TEXT("Assumption is not a report"), C.Review->GetStringField(TEXT("report")), FString(TEXT("none")));
    TestFalse(TEXT("No performed-check event"), C.Events.ContainsByPredicate([](const auto& V) { return V->AsObject()->GetStringField(TEXT("status")) == TEXT("student_report"); }));
    TestFalse(TEXT("No performed-check wording"), C.Describe(2).Contains(TEXT("남긴 기록 · 직접 확인한 결과")));
    C.Choose(TEXT("skip")); const int32 Before = C.Events.Num();
    O->SetStringField(TEXT("stage"), TEXT("choose"));
    C.Consume(Raw(O), 2, {{TEXT("u4"), Quote, TEXT("student"), true}}, TEXT(""));
    TestEqual(TEXT("Assumption cannot reset skipped action"), C.Events.Num(), Before);
    const FString Real = TEXT("빈 배열로 직접 실행해 false를 확인했어요.");
    auto R = Review(Real, TEXT("u5")); R->SetStringField(TEXT("stage"), TEXT("verify")); R->SetStringField(TEXT("trigger"), TEXT("observation")); R->SetStringField(TEXT("report"), TEXT("verification"));
    C.Consume(Raw(R), 2, {{TEXT("u5"), Real, TEXT("student"), true}}, TEXT(""));
    TestTrue(TEXT("Actual student report is retained as a report"), C.Events.ContainsByPredicate([](const auto& V) { return V->AsObject()->GetStringField(TEXT("status")) == TEXT("student_report"); }));
    FKHULearningCoach Reflection;
    const FString OldQuote = TEXT("이전 실행에서 통과했어요.");
    const FString Request = TEXT("아래는 과거 대화에서 학생이 고른 문제 해결 기록 데이터입니다.\n<record_data>\n") + OldQuote + TEXT("\n</record_data>");
    auto Old = Review(OldQuote, TEXT("u6")); Old->SetStringField(TEXT("stage"), TEXT("verify")); Old->SetStringField(TEXT("trigger"), TEXT("observation")); Old->SetStringField(TEXT("report"), TEXT("verification"));
    Reflection.Consume(Raw(Old), 2, {{TEXT("u6"), Request, TEXT("student"), true}}, TEXT(""));
    TestTrue(TEXT("Retrospective still receives help"), Reflection.Review.IsValid());
    TestEqual(TEXT("Old quote not a new result"), Reflection.Review->GetStringField(TEXT("report")), FString(TEXT("none")));
    TestFalse(TEXT("No repeated achievement"), Reflection.Events.ContainsByPredicate([](const auto& V) { return V->AsObject()->GetStringField(TEXT("status")) == TEXT("student_report"); }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachHint, "KHUTutor.Coach.HintBoundaries", Flags)
bool FCoachHint::RunTest(const FString&)
{
    const TArray<FKHUCoachSource> Tool = {{TEXT("tool:GetGraph"), TEXT("L3_SECRET_FIX"), TEXT("tool"), true}};
    FKHULearningCoach C; C.Consume(Raw(Review(TEXT("L3_SECRET_FIX"), TEXT("tool:GetGraph"))), 3, Tool, TEXT(""));
    TestTrue(TEXT("L3 may quote editor evidence"), C.Describe(3).Contains(TEXT("L3_SECRET_FIX")));
    TestTrue(TEXT("Lower hint hides review and action"), C.Describe(0).IsEmpty() && C.Action(0).IsEmpty()); TestFalse(TEXT("Old quote never in state context"), C.Context(3).Contains(TEXT("L3_SECRET_FIX")));
    TestTrue(TEXT("Hidden review is announced without its content"), !C.Notice(0).IsEmpty() && !C.Notice(0).Contains(TEXT("L3_SECRET_FIX")) && C.Notice(3).IsEmpty());
    FKHULearningCoach Low; Low.Consume(Raw(Review(TEXT("L3_SECRET_FIX"), TEXT("tool:GetGraph"))), 0, Tool, TEXT(""));
    TestTrue(TEXT("Low-level review accepted"), Low.Review.IsValid());
    for (int32 Level = 0; Level < 3; ++Level) { TestFalse(TEXT("Below L3 the renderer never quotes editor/tool text"), Low.Describe(Level).Contains(TEXT("L3_SECRET_FIX"))); }
    FKHULearningCoach Own; Own.Consume(Raw(Review()), 0, Sources(), TEXT(""));
    TestTrue(TEXT("A student's own words are shown at every level"), Own.Describe(0).Contains(TEXT("내가 쓴 말: “문이 안 열려요”"))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachReviewText, "KHUTutor.Coach.ReviewEvidenceGuidanceAndReports", Flags)
bool FCoachReviewText::RunTest(const FString&)
{
    auto Add = [](const TSharedRef<FJsonObject>& O, const TCHAR* Item, const TCHAR* Status)
    { auto I = MakeShared<FJsonObject>(); I->Values = O->GetArrayField(TEXT("items"))[0]->AsObject()->Values; I->SetStringField(TEXT("item"), Item); I->SetStringField(TEXT("status"), Status);
      auto Items = O->GetArrayField(TEXT("items")); Items.Add(MakeShared<FJsonValueObject>(I)); O->SetArrayField(TEXT("items"), Items); };
    auto O = Review(); Add(O, TEXT("verification"), TEXT("improve")); Add(O, TEXT("delegation"), TEXT("na")); Add(O, TEXT("thinking"), TEXT("unknown"));
    FKHULearningCoach C; C.Consume(Raw(O), 1, Sources(), TEXT("")); const FString Text = C.Describe(1);
    TestTrue(TEXT("Useful detail is the exact student phrase"), Text.Contains(TEXT("관찰 · 근거 확인")) && Text.Contains(TEXT("내가 쓴 말: “문이 안 열려요”")));
    TestTrue(TEXT("The one improvement has an action, its reason and a fill-in example"), Text.Contains(TEXT("기준 하나를 정해 보세요")) && Text.Contains(TEXT("직접 판단할 수 있어요")) && Text.Contains(TEXT("예: “___")));
    TestTrue(TEXT("Irrelevant and unknown items are not shortcomings"), Text.Contains(TEXT("이번 질문에는 해당 없음")) && Text.Contains(TEXT("판단을 보류했어요")));
    for (const TCHAR* Word : {TEXT("점수"), TEXT("등급"), TEXT("%"), TEXT("khu_review"), TEXT("supported")}) { TestFalse(TEXT("No score, grade or raw contract in the review"), Text.Contains(Word)); }
    auto W = Review(TEXT("변수 이름이 짧아서인 것 같아요")); W->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(TEXT("item"), TEXT("thinking"));
    FKHULearningCoach Wrong; Wrong.Consume(Raw(W), 0, {{TEXT("u1"), TEXT("문이 안 움직이는 게 변수 이름이 짧아서인 것 같아요"), TEXT("student"), true}}, TEXT(""));
    TestTrue(TEXT("A hypothesis is acknowledged without being called correct"), Wrong.Describe(0).Contains(TEXT("생각이 맞는지는 따로 확인해요")));
    const FString Done = TEXT("같은 키를 두 번 넣어 테스트했더니 하나였어요"); const TArray<FKHUCoachSource> S = {{TEXT("u1"), Done, TEXT("student"), true}};
    auto R = Review(Done); R->SetStringField(TEXT("stage"), TEXT("verify")); R->SetStringField(TEXT("trigger"), TEXT("observation")); R->SetStringField(TEXT("report"), TEXT("verification"));
    R->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(TEXT("item"), TEXT("verification"));
    FKHULearningCoach Reported; Reported.Consume(Raw(R), 1, S, TEXT(""));
    TestTrue(TEXT("A performed check is linked as the student's report, not tool verification"), Reported.Describe(1).Contains(TEXT("남긴 기록 · 직접 확인한 결과")) && Reported.Describe(1).Contains(TEXT("도구로 따로 확인한 결과는 아니에요")));
    FKHULearningCoach Copied; Copied.Consume(Raw(R), 1, S, TEXT("AI가 다듬은 문장: ") + Done);
    TestFalse(TEXT("Text copied from an AI reply is never recorded as performed work"), Copied.Describe(1).Contains(TEXT("남긴 기록")));
    FKHULearningCoach Paused; TestTrue(TEXT("No notice by default"), Paused.Notice(1).IsEmpty()); Paused.Choose(TEXT("pause"));
    TestTrue(TEXT("Pause is explained without inventing a diagnosis"), Paused.Notice(1).Contains(TEXT("일시 중지")) && Paused.Describe(1).IsEmpty()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachControl, "KHUTutor.Coach.ControlNoteAndSessionBoundary", Flags)
bool FCoachControl::RunTest(const FString&)
{
    const TArray<FKHUCoachSource> S2 = {{TEXT("u2"), TEXT("빈 입력으로 직접 실행했더니 실패했어요"), TEXT("student"), true}};
    auto Next = [](const TCHAR* Action) { auto O = Review(TEXT("빈 입력으로 직접 실행했더니 실패했어요"), TEXT("u2")); O->SetStringField(TEXT("stage"), TEXT("verify"));
        O->SetStringField(TEXT("trigger"), TEXT("observation")); O->SetStringField(TEXT("report"), TEXT("verification")); O->SetStringField(TEXT("action"), Action); return O; };
    FKHULearningCoach C; TestTrue(TEXT("No control before any choice"), C.Control() == TEXT("none") && C.ControlNote().IsEmpty());
    C.Consume(Raw(Review()), 1, Sources(), TEXT("")); TestEqual(TEXT("An unanswered proposal is not a student choice"), C.Control(), FString(TEXT("none")));
    C.Choose(TEXT("try")); TestTrue(TEXT("Intention is sent as intention"), C.Control() == TEXT("try") && C.ControlNote().StartsWith(TEXT("COACH_CONTROL:")) && C.ControlNote().Contains(TEXT("not a result")));
    C.Consume(Raw(Next(TEXT("none"))), 1, S2, TEXT("")); TestEqual(TEXT("An actual report answers the intention"), C.Control(), FString(TEXT("none")));
    FKHULearningCoach K; K.Consume(Raw(Review()), 1, Sources(), TEXT("")); K.Choose(TEXT("skip"));
    TestTrue(TEXT("Skip forbids repeating the step"), K.Control() == TEXT("skip") && K.ControlNote().Contains(TEXT("Do not repeat that step")));
    K.Consume(Raw(Review()), 1, Sources(), TEXT("")); TestTrue(TEXT("Another message does not lift a skip"), K.Control() == TEXT("skip") && K.Action(1).IsEmpty());
    K.Consume(Raw(Next(TEXT("verify"))), 1, S2, TEXT("")); TestTrue(TEXT("A new observation may open a new step, which supersedes the old skip"), !K.Action(1).IsEmpty() && K.Control() == TEXT("none"));
    K.Choose(TEXT("hint")); TestTrue(TEXT("Hint request never asks the model to raise the level"), K.Control() == TEXT("hint") && K.ControlNote().Contains(TEXT("never raise it yourself")));
    K.Choose(TEXT("pause")); TestTrue(TEXT("Pause outranks earlier choices"), K.Control() == TEXT("paused") && K.ControlNote().Contains(TEXT("paused")));
    FKHUChatRequest R; R.Model = TEXT("model"); R.Hint = 1; R.AssetScope = TEXT("/Game/Door"); R.CoachControl = TEXT("none");
    const auto Stored = KHUClaudeProtocol::SessionIdentity(TEXT("P"), R, TEXT("F"), TEXT("instructions v1"));
    const FString Session = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower); Stored->SetStringField(TEXT("session"), Session); Stored->SetBoolField(TEXT("usable"), true);
    FString Id; TestTrue(TEXT("Unchanged conversation resumes its session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), R, TEXT("F"), TEXT("instructions v1")), Id) && Id == Session);
    TestFalse(TEXT("Changed product instructions start a fresh session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), R, TEXT("F"), TEXT("instructions v2")), Id));
    auto Changed = R; Changed.CoachControl = TEXT("try"); TestTrue(TEXT("An intention keeps the session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), Changed, TEXT("F"), TEXT("instructions v1")), Id));
    for (const TCHAR* Declined : {TEXT("skip"), TEXT("paused")})
    { Changed.CoachControl = Declined; TestFalse(TEXT("A declined or paused exercise leaves the replayed transcript"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), Changed, TEXT("F"), TEXT("instructions v1")), Id)); }
    Changed = R; Changed.Hint = 0; TestFalse(TEXT("A lower hint never resumes a higher-hint session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), Changed, TEXT("F"), TEXT("instructions v1")), Id));
    Changed = R; Changed.AssetScope = TEXT("/Game/Other"); TestFalse(TEXT("Another asset has its own session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), Changed, TEXT("F"), TEXT("instructions v1")), Id));
    TestFalse(TEXT("Another folder has its own session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), R, TEXT("Other"), TEXT("instructions v1")), Id));
    TestFalse(TEXT("Another project has its own session"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("Q"), R, TEXT("F"), TEXT("instructions v1")), Id));
    const auto Legacy = MakeShared<FJsonObject>(); Legacy->Values = Stored->Values; Legacy->RemoveField(TEXT("coach_boundary"));
    TestFalse(TEXT("A session file from before this field is replaced once"), KHUClaudeProtocol::CanResume(Legacy, KHUClaudeProtocol::SessionIdentity(TEXT("P"), R, TEXT("F"), TEXT("instructions v1")), Id));
    Stored->SetBoolField(TEXT("usable"), false); TestFalse(TEXT("An interrupted session is never resumed"), KHUClaudeProtocol::CanResume(Stored, KHUClaudeProtocol::SessionIdentity(TEXT("P"), R, TEXT("F"), TEXT("instructions v1")), Id)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachStorage, "KHUTutor.Coach.StorageLegacyScopeAndFailure", Flags)
bool FCoachStorage::RunTest(const FString&)
{
    auto C = Config(); const FString Path = File(); FKHUProjectMemory M(Path, TEXT("P"), C.Context); FString Id, Folder;
    M.CreateFolder(TEXT("과제"), Folder); M.CreateConversation(TEXT("/Game/Door"), Folder, 1, Id); FKHUSavedConversation Saved;
    TestTrue(TEXT("Existing chat without optional coach loads"), M.ReadConversation(Id, 1, Saved) && !Saved.Coach);
    FKHULearningCoach Coach; Coach.Consume(Raw(Review()), 1, Sources(), TEXT("")); Coach.Choose(TEXT("try")); Coach.Choose(TEXT("pause"));
    TestTrue(TEXT("State stored"), M.SaveCoach(Id, TEXT("/Game/Door"), Coach.Save())); FKHUProjectMemory Restart(Path, TEXT("P"), C.Context);
    TestTrue(TEXT("State reopened"), Restart.ReadConversation(Id, 1, Saved)); FKHULearningCoach Loaded; Loaded.Restore(Saved.Coach);
    TestTrue(TEXT("Pause persisted"), Loaded.bPaused); TestEqual(TEXT("Choice retained"), Loaded.Review->GetStringField(TEXT("status")), FString(TEXT("try")));
    TestFalse(TEXT("Asset isolation"), M.SaveCoach(Id, TEXT("/Game/Other"), Coach.Save())); FKHUProjectMemory Foreign(Path, TEXT("OtherProject"), C.Context);
    TestFalse(TEXT("Project isolation"), Foreign.SaveCoach(Id, TEXT("/Game/Door"), Coach.Save()));
    const auto Before = Loaded.Save(); auto Tiny = C.Context; Tiny.ArchiveMaxBytes = 1; FKHUProjectMemory Blocked(Path, TEXT("P"), Tiny);
    Loaded.Choose(TEXT("resume")); TestFalse(TEXT("Storage limit is explicit failure"), Blocked.SaveCoach(Id, TEXT("/Game/Door"), Loaded.Save()));
    Restart.ReadConversation(Id, 1, Saved); TestEqual(TEXT("Failed save preserves choices"), KHUMcpProtocol::WriteObject(Saved.Coach.ToSharedRef()), KHUMcpProtocol::WriteObject(Before));
    auto Bad = Coach.Save(); Bad->SetNumberField(TEXT("version"), 999); TestFalse(TEXT("Bad state cannot overwrite"), M.SaveCoach(Id, TEXT("/Game/Door"), Bad)); M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachLoop, "KHUTutor.Coach.AnswerLoopAndCancellation", Flags)
bool FCoachLoop::RunTest(const FString&)
{
    auto C = Config(); auto Chat = MakeShared<FChat>(); auto Agent = Loop(C, Chat); auto Memory = MakeShared<FKHUProjectMemory>(File(), TEXT("P"), C.Context);
    Agent->SetProjectMemory(Memory, TEXT("")); TestTrue(TEXT("Answer started"), Agent->Ask(TEXT("문이 안 열려요"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}));
    TestFalse(TEXT("Raw JSON never reaches visible reply"), Agent->Receipt()->GetStringField(TEXT("text")).Contains(TEXT("khu_review")));
    TestTrue(TEXT("Live review connected"), Agent->LearningCoach().Review.IsValid());
    const FString Id = Agent->LearningCoach().Id(); TestFalse(TEXT("Stale button rejected"), Agent->ChooseCoaching(TEXT("skip"), TEXT("old"))); TestTrue(TEXT("Choice saved"), Agent->ChooseCoaching(TEXT("skip"), Id));
    TestTrue(TEXT("Follow-up not blocked"), Agent->FollowUp(TEXT("문이 안 열려요"), 0));
    TestEqual(TEXT("Explicit skip reaches runtime control"), Chat->Requests.Last().CoachControl, FString(TEXT("skip")));
    const FString Request = KHUChatProtocol::Body(Chat->Requests.Last()); for (const auto& V : Chat->Requests.Last().Messages) { const auto Msg = V->AsObject(); if (Msg->GetStringField(TEXT("role")) == TEXT("assistant")) { TestFalse(TEXT("No metadata in assistant context"), Msg->GetStringField(TEXT("content")).Contains(TEXT("<khu_review>"))); } } TestTrue(TEXT("Latest suppression sent"), Request.Contains(TEXT("skip")));
    auto ControlNotes = [&Chat] { int32 Count = 0; for (const auto& V : Chat->Requests.Last().Messages) { const auto Msg = V->AsObject(); if (Msg->GetStringField(TEXT("role")) == TEXT("system") && Msg->GetStringField(TEXT("content")).StartsWith(TEXT("COACH_CONTROL:"))) { ++Count; } } return Count; };
    TestEqual(TEXT("Skip reaches the model as one application note"), ControlNotes(), 1);
    TestTrue(TEXT("Second follow-up not blocked"), Agent->FollowUp(TEXT("그럼 변수는 뭐예요?"), 0)); TestEqual(TEXT("Control note is replaced, not accumulated"), ControlNotes(), 1);
    FKHUSavedConversation Saved; const FString Task = Agent->Receipt()->GetStringField(TEXT("task_id")); Memory->ReadConversation(Task, 0, Saved); TestTrue(TEXT("High assistant reply hidden"), Saved.HiddenReplies > 0);
    Agent->Cancel(); auto Deferred = MakeShared<FChat>(); Deferred->Deferred = true; auto Late = Loop(C, Deferred); Late->SetProjectMemory(Memory, TEXT("")); Late->Ask(TEXT("문이 안 열려요"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {});
    Late->Cancel(); Deferred->Finish(Deferred->Pending); TestFalse(TEXT("Late completion cannot store proposal"), Late->LearningCoach().Review.IsValid()); Memory->Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachForgedState, "KHUTutor.Coach.StoredStateCannotChangeAuthority", Flags)
bool FCoachForgedState::RunTest(const FString&)
{
    FKHULearningCoach C; C.Consume(Raw(Review()), 1, Sources(), TEXT("")); auto O = C.Save(); O->GetObjectField(TEXT("review"))->SetStringField(TEXT("status"), TEXT("mastered"));
    FKHULearningCoach Other; TestFalse(TEXT("Unknown mastery state refused"), Other.Restore(O)); TestTrue(TEXT("Invalid review cannot display"), Other.Describe(3).IsEmpty()); return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachPanel, "KHUTutor.Coach.PanelHintAndChoiceFailure", Flags)
bool FCoachPanel::RunTest(const FString&)
{
    const auto C = Config(); const FString Path = File(); auto Panel = SNew(SKHUTutorPanel);
    Panel->ProjectMemory = MakeShared<FKHUProjectMemory>(Path, TEXT("P"), C.Context); Panel->Hint = 3;
    FString Id; Panel->ProjectMemory->CreateConversation(TEXT(""), TEXT(""), 3, Id);
    Panel->ProjectMemory->RecordTurn(Id, TEXT(""), TEXT("목표"), TEXT("assistant"), TEXT("HIGH_HINT_SECRET"), TEXT("a1"), 3, 1);
    FKHULearningCoach Coach; Coach.Consume(Raw(Review()), 3, Sources(), TEXT("")); Panel->ProjectMemory->SaveCoach(Id, TEXT(""), Coach.Save());
    Panel->OpenConversation(Id); TestEqual(TEXT("High hint original available"), Panel->StoredConversation.Turns.Num(), 1);
    TestEqual(TEXT("Reopened card is already marked shown before any failed request"), Panel->ShownReview, Coach.Id());
    TFunction<int32(TSharedRef<SWidget>)> CountReviews;
    CountReviews = [&CountReviews, this](TSharedRef<SWidget> Widget)
    { int32 Count = 0; if (Widget->GetTypeAsString() == TEXT("SExpandableArea"))
      { ++Count; TestFalse(TEXT("Review starts collapsed"), StaticCastSharedRef<SExpandableArea>(Widget)->IsExpanded()); }
      FChildren* Children = Widget->GetChildren(); for (int32 I = 0; I < Children->Num(); ++I) { Count += CountReviews(Children->GetChildAt(I)); } return Count; };
    TestEqual(TEXT("Review widget built with stored conversation"), CountReviews(Panel->Conversation.ToSharedRef()), 1);
    Panel->ChangeHint(0, false); TestEqual(TEXT("Hint change immediately re-reads filtered history"), Panel->StoredConversation.Turns.Num(), 0);
    TestEqual(TEXT("Hidden high reply counted"), Panel->StoredConversation.HiddenReplies, 1);
    Panel->Input->SetText(FText::FromString(TEXT("자유롭게 질문"))); TestEqual(TEXT("Coaching never consumes draft"), Panel->Input->GetText().ToString(), FString(TEXT("자유롭게 질문")));
    auto Chat = MakeShared<FChat>(); auto Agent = Loop(C, Chat); Agent->SetProjectMemory(Panel->ProjectMemory, TEXT(""));
    Agent->Ask(TEXT("문이 안 열려요"), TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, false, true, Id);
    const FString State = KHUMcpProtocol::WriteObject(Agent->LearningCoach().Save()), ReviewId = Agent->LearningCoach().Id();
    FString Original; FFileHelper::LoadFileToString(Original, *Path); FFileHelper::SaveStringToFile(TEXT("broken-store"), *Path);
    TestFalse(TEXT("Failed selection persistence is reported"), Agent->ChooseCoaching(TEXT("pause"), ReviewId));
    TestEqual(TEXT("Failed selection leaves in-memory choice unchanged"), KHUMcpProtocol::WriteObject(Agent->LearningCoach().Save()), State);
    FFileHelper::SaveStringToFile(Original, *Path); Agent->Cancel();
    FKHULearningCoach Missing; Missing.Consume(TEXT("본문만 있는 답변"), 1, {}, TEXT(""));
    Panel->ClearConversation(); Panel->Hint = 1; Panel->AddCoachCard(Missing);
    TestEqual(TEXT("Missing metadata has an explicit collapsed availability card"), CountReviews(Panel->Conversation.ToSharedRef()), 1);
    TestTrue(TEXT("Availability card has no student task"), Missing.Action(1).IsEmpty() && Missing.Describe(1).IsEmpty());
    Panel->Stop(); Panel->ProjectMemory->Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoachLive, "KHUTutor.Coach.Live.StudentResponses", Flags)
bool FCoachLive::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorCoachLive"))) { AddInfo(TEXT("Paid E1 semantic fixtures not requested")); return true; }
    FString Error, Key;
    if (!FKHUSecretStore(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi")).Load(Key, Error)) { AddError(Error); return false; }
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FAutomationTestBase* Test; FKHUConfig C; FString Key, Id, Path, Tier = TEXT("T1"); int32 Stage = 0; double Deadline = 0;
        TSharedPtr<FKHUProjectMemory> Memory; TSharedPtr<FKHUAgentLoop> Agent; TSharedPtr<FRecorder> Client;
        const TArray<FString> Questions = {
            TEXT("문이 안 열려요. 목표는 버튼을 누르면 열리는 거예요. 실행해 보니 그대로 닫혀 있어요."),
            TEXT("현재 제가 만들려는 기능을 자세하게 설명드리면 버튼을 눌렀을 때 문이 열리는 기능입니다. 실제로 실행해서 확인한 결과 버튼을 눌렀는데도 문이 그대로 닫혀 있었습니다. 이 문제를 도와주세요."),
            TEXT("Blueprint에서 변수란 뭐예요?"),
            TEXT("합성 프로젝트 관찰이에요. 버튼을 눌렀을 때 문이 열려야 해요. 제가 확인한 건 클릭 로그는 나오고 문은 움직이지 않는다는 거예요. 에디터를 읽지 말고 이 관찰만으로 다음 한 가지를 도와주세요."),
            TEXT("문이 안 움직이는 게 변수 이름이 짧아서인 것 같아요. 사실 잘 모르겠어요. 아주 작은 관찰부터 도와주세요."),
            TEXT("모르겠어요. 기대 결과와 실제 결과를 비교한다는 게 무슨 뜻이에요?"),
            TEXT("이전 답변이 API 연결 오류로 안 왔어요. 그래서 같은 질문을 다시 보냅니다. Blueprint 변수의 역할을 설명해 주세요."),
            TEXT("인벤토리를 전부 만들어 줘. 어떤 걸 배워야 할지도 아직 모르겠어요."),
            TEXT("합성 코드 검토예요. AI가 준 C++ 함수는 int First(const TArray<int>& Values) { return Values[0]; } 입니다. 컴파일은 됐지만 실행은 아직 안 했어요. 이걸 써도 될까요?"),
            TEXT("AI가 다듬어 준 질문을 그대로 보냅니다: 목표는 문 열기이며 기대 결과와 실제 결과를 비교하고 싶습니다. 아직 제가 직접 판단하거나 실행한 건 없어요."),
            TEXT("합성 수행 보고예요. 배열과 맵을 비교해서 중복 키 갱신이 필요해 맵을 골랐어요. 직접 코드를 바꾸고 같은 키를 두 번 넣어 결과가 하나인지 테스트했더니 하나였어요. 선택 이유와 테스트에서 보완할 한 가지만 알려주세요."),
            // A card proposal must exist before "skip" can be exercised; adopting unrun AI code is such a step.
            TEXT("합성 학습 과제예요. AI가 준 C++ 함수 int First(const TArray<int>& Values) { return Values[0]; } 를 프로젝트에 그대로 넣으려고 해요. 컴파일은 됐고 실행은 아직 안 했어요."),
            TEXT("아직 새 관찰은 없어요. 같은 문제를 이어서 도와주세요."),
            TEXT("자동 코칭 없이 지금 질문에 답해주세요. 변수란 무엇인가요?"),
            TEXT("힌트를 낮췄어요. 아직 직접 테스트하지 않았어요. 제가 모르는 게 뭔지부터 도와주세요.")};
        int32 Last = 14;
        // Cases 0-11 each open their own conversation; 12-14 continue case 11, so a partial run may start only at 0-11.
        FCheck(FAutomationTestBase* T, FString K) : Test(T), C(Config()), Key(MoveTemp(K))
        { FParse::Value(FCommandLine::Get(), TEXT("KHUTutorCoachStart="), Stage); FParse::Value(FCommandLine::Get(), TEXT("KHUTutorCoachEnd="), Last); Stage = FMath::Clamp(Stage, 0, 11); Last = FMath::Clamp(Last, Stage, Questions.Num() - 1);
          // A different configured tier (for example the Haiku comparison tier) runs the same fixtures; unknown names fall back to the student tier.
          FParse::Value(FCommandLine::Get(), TEXT("KHUTutorCoachTier="), Tier); if (!C.Tiers.Contains(Tier)) { Tier = TEXT("T1"); } Start(); }
        ~FCheck() override { if (Agent) { Agent->Cancel(); } if (Memory) { Memory->Forget(); } }
        void Start()
        {
            if (Stage < 12)
            {
                if (Memory) { Memory->Forget(); }
                Path = File(); Memory = MakeShared<FKHUProjectMemory>(Path, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), C.Context);
                if (!Memory->CreateConversation(TEXT(""), TEXT(""), Level(), Id)) { Test->AddError(Memory->Error()); Stage = Questions.Num(); return; }
            }
            Client = MakeShared<FRecorder>(C, Key, Memory); Agent = Loop(C, Client.ToSharedRef()); Agent->SetProjectMemory(Memory, TEXT(""));
            Deadline = FPlatformTime::Seconds() + 240;
            const FString Input = TEXT("[개발 검수용 합성 자료: 실제 학생/자산 기록이 아닙니다. 이 질문에 제공된 내용만 사용하고 도구를 호출하지 마세요.]\n") + Questions[Stage];
            Test->TestTrue(TEXT("Student model fixture started"), Agent->Ask(Input, Tier, Level(), [](EKHUAgentState, const FString&) {}, true, false, Id));
        }
        // 11-13 share one level so that only the student's choice, not a hint change, can replace the session.
        int32 Level() const { return Stage == 3 || Stage == 4 || Stage == 14 ? 0 : Stage == 8 || Stage >= 11 ? 2 : 1; }
        virtual bool Update() override
        {
            if (Stage > Last) { return true; }
            if (FPlatformTime::Seconds() > Deadline) { Agent->Cancel(); Test->AddError(TEXT("E1 live timeout")); return true; }
            if (Agent->State() != EKHUAgentState::Idle && Agent->State() != EKHUAgentState::AskContinue) { return false; }
            const auto R = Agent->Receipt(); R->SetStringField(TEXT("fixture_question"), Questions[Stage]); R->SetNumberField(TEXT("fixture_hint"), Level()); R->SetStringField(TEXT("fixture_tier"), Tier); R->SetStringField(TEXT("raw_response"), Client->RawText);
            R->SetStringField(TEXT("review_display"), Agent->LearningCoach().Describe(Level())); R->SetStringField(TEXT("action_display"), Agent->LearningCoach().Action(Level()));
            R->SetStringField(TEXT("notice_display"), Agent->LearningCoach().Notice(Level())); R->SetStringField(TEXT("control_sent"), Client->Control);
            const auto& Usage = R->GetArrayField(TEXT("usage"));
            const bool Ok = Agent->State() == EKHUAgentState::Idle && Usage.Num() > 0 && Usage.Last()->AsObject()->GetBoolField(TEXT("success"))
                && !R->GetStringField(TEXT("text")).TrimStartAndEnd().IsEmpty();
            Test->TestTrue(TEXT("Actual response completed"), Ok);
            if (Stage == 12 || Stage == 13 || Stage == 14)
            { if (!Usage.IsEmpty()) { const auto Runtime = Usage.Last()->AsObject()->GetObjectField(TEXT("runtime")); Test->TestFalse(TEXT("Explicit choice/hint changes rotate stale CLI session"), Runtime->GetBoolField(TEXT("resumed"))); } }
            if (Stage >= 12) { Test->TestTrue(TEXT("No repeated/paused/downward action"), Agent->LearningCoach().Action(Level()).IsEmpty()); }
            if (Stage == 11 && Ok) { Test->TestTrue(TEXT("Fixture produced actionable review"), !Agent->LearningCoach().Action(Level()).IsEmpty()); Agent->ChooseCoaching(TEXT("skip"), Agent->LearningCoach().Id()); }
            if (Stage == 12 && Ok) { Agent->ChooseCoaching(TEXT("pause"), Agent->LearningCoach().Id()); }
            R->SetObjectField(TEXT("state_after_choice"), Agent->LearningCoach().Save());
            const FString Output = FPaths::ProjectSavedDir() / TEXT("KHUTutor") / FString::Printf(TEXT("coach_live_%02d.json"), Stage);
            Test->TestTrue(TEXT("Question context hint response usage retained"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(R), *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            Agent->Cancel(); Agent.Reset(); if (!Ok || ++Stage > Last) { return true; } Start(); return false;
        }
    };
    AddCommand(new FCheck(this, MoveTemp(Key))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealProjectLive, "KHUTutor.Coach.Live.RealProject", Flags)
bool FRealProjectLive::RunTest(const FString&)
{
    // Opt-in paid run driven by a JSON file: {"tier":"T1","deadline_seconds":300,"questions":[{"hint":1,"text":"..."}]}.
    // Unlike the synthetic fixtures, editor tools stay available, so the model can read real build errors and sources.
    FString Spec; if (!FParse::Value(FCommandLine::Get(), TEXT("KHUTutorRealLive="), Spec)) { AddInfo(TEXT("Paid real-project fixtures not requested")); return true; }
    FString Error, Key;
    if (!FKHUSecretStore(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi")).Load(Key, Error)) { AddError(Error); return false; }
    FString Text; TSharedPtr<FJsonObject> Plan; const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!FFileHelper::LoadFileToString(Text, *Spec) || !KHUMcpProtocol::ReadObject(Text, Plan) || !Plan->TryGetArrayField(TEXT("questions"), Items) || Items->IsEmpty() || Items->Num() > 20)
    { AddError(TEXT("Real-project fixture file unreadable: ") + Spec); return false; }
    class FRun final : public IAutomationLatentCommand
    {
    public:
        FAutomationTestBase* Test; FKHUConfig C; FString Key, Id, Path, Tier; TArray<TPair<int32, FString>> Questions; int32 Stage = 0; double Deadline = 0, Seconds = 300;
        TSharedPtr<FKHUProjectMemory> Memory; TSharedPtr<FKHUAgentLoop> Agent; TSharedPtr<FRecorder> Client;
        FRun(FAutomationTestBase* T, FString K, const TSharedPtr<FJsonObject>& P) : Test(T), C(Config()), Key(MoveTemp(K))
        {
            P->TryGetStringField(TEXT("tier"), Tier); if (!C.Tiers.Contains(Tier)) { Tier = TEXT("T1"); }
            double D = 0; if (P->TryGetNumberField(TEXT("deadline_seconds"), D) && D >= 60 && D <= 900) { Seconds = D; }
            for (const auto& V : P->GetArrayField(TEXT("questions")))
            { const auto Q = V->AsObject(); double Hint = 1; FString Body; Q->TryGetNumberField(TEXT("hint"), Hint); Q->TryGetStringField(TEXT("text"), Body);
              if (!Body.IsEmpty()) { Questions.Add({FMath::Clamp(static_cast<int32>(Hint), 0, 3), Body}); } }
            Path = File(); Memory = MakeShared<FKHUProjectMemory>(Path, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), C.Context);
            if (Questions.IsEmpty() || !Memory->CreateConversation(TEXT(""), TEXT(""), Questions[0].Key, Id)) { Test->AddError(TEXT("Real-project conversation not created")); Stage = Questions.Num(); return; }
            Start();
        }
        ~FRun() override { if (Agent) { Agent->Cancel(); } if (Memory) { Memory->Forget(); } }
        void Start()
        {
            // The same conversation is reopened for every question, as the panel does; tools and memory stay enabled.
            Client = MakeShared<FRecorder>(C, Key, Memory); Agent = Loop(C, Client.ToSharedRef()); Agent->SetProjectMemory(Memory, TEXT(""));
            Deadline = FPlatformTime::Seconds() + Seconds;
            Test->TestTrue(TEXT("Real-project question started"), Agent->Ask(Questions[Stage].Value, Tier, Questions[Stage].Key, [](EKHUAgentState, const FString&) {}, true, false, Id));
        }
        virtual bool Update() override
        {
            if (Stage >= Questions.Num()) { return true; }
            if (FPlatformTime::Seconds() > Deadline) { Agent->Cancel(); Test->AddError(TEXT("Real-project live timeout")); return true; }
            if (Agent->State() != EKHUAgentState::Idle && Agent->State() != EKHUAgentState::AskContinue) { return false; }
            const int32 Hint = Questions[Stage].Key; const auto R = Agent->Receipt();
            R->SetStringField(TEXT("fixture_question"), Questions[Stage].Value); R->SetNumberField(TEXT("fixture_hint"), Hint); R->SetStringField(TEXT("fixture_tier"), Tier); R->SetStringField(TEXT("raw_response"), Client->RawText);
            R->SetStringField(TEXT("review_display"), Agent->LearningCoach().Describe(Hint)); R->SetStringField(TEXT("action_display"), Agent->LearningCoach().Action(Hint));
            R->SetStringField(TEXT("notice_display"), Agent->LearningCoach().Notice(Hint)); R->SetStringField(TEXT("control_sent"), Client->Control);
            const auto& Usage = R->GetArrayField(TEXT("usage"));
            const bool Ok = Agent->State() == EKHUAgentState::Idle && Usage.Num() > 0 && Usage.Last()->AsObject()->GetBoolField(TEXT("success"))
                && !R->GetStringField(TEXT("text")).TrimStartAndEnd().IsEmpty();
            Test->TestTrue(TEXT("Real-project response completed"), Ok);
            const FString Output = FPaths::ProjectSavedDir() / TEXT("KHUTutor") / FString::Printf(TEXT("real_live_%02d.json"), Stage);
            Test->TestTrue(TEXT("Real-project receipt retained"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(R), *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            Agent->Cancel(); Agent.Reset(); if (!Ok || ++Stage >= Questions.Num()) { return true; } Start(); return false;
        }
    };
    AddCommand(new FRun(this, MoveTemp(Key), Plan)); return true;
}
#endif

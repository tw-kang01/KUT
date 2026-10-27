#include "AI/KHUClaudeRuntime.h"
#include "AI/KHUAgentLoop.h"
#include "AI/KHUHintPolicy.h"
#include "AI/KHUSecretStore.h"
#include "KHUTutorModule.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FKHUConfig Config() { return IKHUTutorModule::Get().PanelConfig(); }
    TSharedRef<FJsonObject> Json(const FString& Text) { TSharedPtr<FJsonObject> O; KHUMcpProtocol::ReadObject(Text, O); return O.ToSharedRef(); }
    FString File() { return FPaths::ProjectSavedDir() / TEXT("KHUTutor/ClaudeTests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeQuote, "KHUTutor.Claude.WindowsArguments", Flags)
bool FClaudeQuote::RunTest(const FString&)
{
    TestEqual(TEXT("Empty argument stays an argument"), KHUClaudeProtocol::Quote(TEXT("")), FString(TEXT("\"\"")));
    TestEqual(TEXT("Space and Korean"), KHUClaudeProtocol::Quote(TEXT("C:\\문 폴더\\")), FString(TEXT("\"C:\\문 폴더\\\\\"")));
    TestEqual(TEXT("Quote escaped"), KHUClaudeProtocol::Quote(TEXT("a\"b")), FString(TEXT("\"a\\\"b\""))); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeUsage, "KHUTutor.Claude.PerRunUsageAndCache", Flags)
bool FClaudeUsage::RunTest(const FString&)
{
    auto C = Config(); FKHUPrice Price; Price.Input = 2; Price.Output = 10; Price.CachedInput = .2; Price.CacheWrite = 2.5; Price.CacheWriteHour = 4; Price.CreditsPerUsd = 1000; C.Prices.Add(TEXT("fixture"), Price);
    const auto Event = Json(TEXT("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"이어받음\",\"num_turns\":3,\"total_cost_usd\":900,\"modelUsage\":{\"fixture\":{\"costUSD\":900}},\"usage\":{\"input_tokens\":10,\"output_tokens\":20,\"cache_creation_input_tokens\":100,\"cache_read_input_tokens\":200,\"cache_creation\":{\"ephemeral_1h_input_tokens\":40}}}"));
    const auto R = KHUClaudeProtocol::Result(Event, C, TEXT("fixture"));
    TestTrue(TEXT("Success"), R.bSuccess && R.bUsageKnown && R.bAggregateUsage); TestEqual(TEXT("All three model turns accounted together"), R.Attempts, 3);
    TestTrue(TEXT("Per-run Gateway pricing instead of cumulative CLI USD"), R.Credits.IsSet() && FMath::IsNearlyEqual(R.Credits.GetValue(), .57));
    TestEqual(TEXT("Actual input includes cached/written input"), R.InputTokens, int64(310));
    C.Prices[TEXT("fixture")].CreditsPerUsd.Reset(); TestFalse(TEXT("Unknown conversion stays unknown"), KHUClaudeProtocol::Result(Event, C, TEXT("fixture")).Credits.IsSet());
    Event->GetObjectField(TEXT("usage"))->SetNumberField(TEXT("input_tokens"), -1); TestFalse(TEXT("Invalid usage rejected"), KHUClaudeProtocol::Result(Event, C, TEXT("fixture")).bUsageKnown); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeLimits, "KHUTutor.Claude.ResumableLimits", Flags)
bool FClaudeLimits::RunTest(const FString&)
{
    const auto Turn = KHUClaudeProtocol::Result(Json(TEXT("{\"subtype\":\"error_max_turns\",\"is_error\":true,\"num_turns\":3}")), Config(), TEXT("fixture"));
    TestTrue(TEXT("Iteration limit offered as Continue"), !Turn.bSuccess && Turn.bIterationLimit && !Turn.bBudget && Turn.Error.Contains(TEXT("계속하기")));
    const auto Budget = KHUClaudeProtocol::Result(Json(TEXT("{\"subtype\":\"error_max_budget_usd\",\"is_error\":true}")), Config(), TEXT("fixture")); TestTrue(TEXT("Budget identified separately"), Budget.bBudget && !Budget.bIterationLimit); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeContinuation, "KHUTutor.Claude.TruncatedCoachTrailerKeepsBody", Flags)
bool FClaudeContinuation::RunTest(const FString&)
{
    const FString Trailer = TEXT("<khu_review>{\"report\":\"none\"}</khu_review>");
    auto Final = Json(TEXT("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"session_id\":\"S\",\"num_turns\":2}")); Final->SetStringField(TEXT("result"), Trailer);
    auto First = Json(TEXT("{\"type\":\"assistant\",\"session_id\":\"S\",\"parent_tool_use_id\":null,\"message\":{\"stop_reason\":\"max_tokens\",\"content\":[]}}"));
    auto Block = Json(TEXT("{\"type\":\"text\"}")); Block->SetStringField(TEXT("text"), TEXT("실행 미확인. 팀원이 호출부를 수정했습니다.\n<khu_review>{\"report\":"));
    auto Thinking = Json(TEXT("{\"type\":\"thinking\",\"thinking\":\"PRIVATE_REASONING_SENTINEL\"}"));
    First->GetObjectField(TEXT("message"))->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Thinking), MakeShared<FJsonValueObject>(Block)});
    auto Last = Json(TEXT("{\"type\":\"assistant\",\"session_id\":\"S\",\"message\":{\"stop_reason\":\"end_turn\",\"content\":[]}}"));
    auto LastBlock = Json(TEXT("{\"type\":\"text\"}")); LastBlock->SetStringField(TEXT("text"), Trailer);
    Last->GetObjectField(TEXT("message"))->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(LastBlock)});
    TArray<TSharedPtr<FJsonValue>> Stream = {MakeShared<FJsonValueObject>(First), MakeShared<FJsonValueObject>(Last)};
    const auto Recovered = KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream);
    TestEqual(TEXT("Visible body plus completed metadata"), Recovered.Content, FString(TEXT("실행 미확인. 팀원이 호출부를 수정했습니다.\n\n")) + Trailer);
    TestFalse(TEXT("Never expose reasoning"), Recovered.Content.Contains(TEXT("PRIVATE_REASONING_SENTINEL")));
    TestEqual(TEXT("Assistant archive gets same body"), Recovered.Assistant->GetStringField(TEXT("content")), Recovered.Content);
    TestEqual(TEXT("Aggregate billing unchanged"), Recovered.Attempts, 2);
    First->SetStringField(TEXT("session_id"), TEXT("FOREIGN"));
    TestEqual(TEXT("Other session excluded"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream).Content, Trailer);
    First->SetStringField(TEXT("session_id"), TEXT("S")); First->SetStringField(TEXT("parent_tool_use_id"), TEXT("subagent"));
    TestEqual(TEXT("Nested agent excluded"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream).Content, Trailer); First->SetField(TEXT("parent_tool_use_id"), MakeShared<FJsonValueNull>());
    Stream.Insert(MakeShared<FJsonValueObject>(Json(TEXT("{\"type\":\"user\"}"))), 1);
    TestEqual(TEXT("Tool/user boundary excluded"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream).Content, Trailer); Stream.RemoveAt(1);
    First->GetObjectField(TEXT("message"))->SetStringField(TEXT("stop_reason"), TEXT("end_turn"));
    TestEqual(TEXT("Unrelated completed answer excluded"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream).Content, Trailer);
    First->GetObjectField(TEXT("message"))->SetStringField(TEXT("stop_reason"), TEXT("max_tokens"));
    Final->SetStringField(TEXT("result"), TEXT("새로운 완전한 답변"));
    TestEqual(TEXT("Full replacement is not duplicated"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream).Content, FString(TEXT("새로운 완전한 답변")));
    Final->SetStringField(TEXT("result"), Trailer); Final->SetBoolField(TEXT("is_error"), true);
    TestFalse(TEXT("Failed run not upgraded"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream).bSuccess);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeSupplementContinuation, "KHUTutor.Claude.TruncatedCoachSupplementKeepsDraft", Flags)
bool FClaudeSupplementContinuation::RunTest(const FString&)
{
    // Normalized protocol replay of blind_e2e3/20261018_revision1 case 2:
    // a complete draft, an interrupted review, CLI auto-continuation/compaction, then a supplement.
    // The session transcript survives; stdout was not saved. CLI 2.1.296 maps isMeta/compact
    // summaries to stream isSynthetic, and may emit assistant blocks with stop_reason=null.
    const FString Body = TEXT("학생 확인 전 AI 초안\n문제: 소수 비율 표현.\n선택 이유: 소수 부분 보존.\n기여: 학생 선택, AI 제안, 팀원 컴파일은 전언.\n검증: 수정·빌드·실행 미확인.\n다음 계획: 직접 실행 후 기록.");
    const FString Supplement = TEXT("앞 회고 초안은 마지막 검토 카드 부분이 잘렸습니다. 검증 결과는 아직 미확인입니다.");
    const FString Trailer = TEXT("<khu_review>{\"report\":\"none\"}</khu_review>");
    auto Assistant = [](const FString& Text, const FString& Stop)
    {
        auto E = Json(TEXT("{\"type\":\"assistant\",\"session_id\":\"S\",\"parent_tool_use_id\":null,\"message\":{\"content\":[],\"stop_reason\":null}}"));
        auto Block = Json(TEXT("{\"type\":\"text\"}")); Block->SetStringField(TEXT("text"), Text);
        E->GetObjectField(TEXT("message"))->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Block)});
        if (!Stop.IsEmpty()) { E->GetObjectField(TEXT("message"))->SetStringField(TEXT("stop_reason"), Stop); }
        return E;
    };
    auto First = Assistant(Body + TEXT("\n<khu_review>{\"items\":[{\"quote\":\"cut"), TEXT(""));
    auto Continue = Json(TEXT("{\"type\":\"user\",\"session_id\":\"S\",\"parent_tool_use_id\":null,\"isSynthetic\":true,\"message\":{\"role\":\"user\"}}"));
    Continue->GetObjectField(TEXT("message"))->SetStringField(TEXT("content"), TEXT("Output token limit hit. Resume directly — no apology, no recap of what you were doing. Pick up mid-thought if that is where the cut happened. Break remaining work into smaller pieces."));
    auto Compact = Json(TEXT("{\"type\":\"system\",\"subtype\":\"compact_boundary\",\"session_id\":\"S\"}"));
    auto Summary = Json(TEXT("{\"type\":\"user\",\"session_id\":\"S\",\"parent_tool_use_id\":null,\"isSynthetic\":true,\"message\":{\"role\":\"user\",\"content\":\"PRIVATE_COMPACT_SUMMARY\"}}"));
    auto Thinking = Json(TEXT("{\"type\":\"assistant\",\"session_id\":\"S\",\"parent_tool_use_id\":null,\"message\":{\"stop_reason\":null,\"content\":[{\"type\":\"thinking\",\"thinking\":\"PRIVATE_REASONING\"}]}}"));
    auto Last = Assistant(Supplement, TEXT(""));
    auto Final = Json(TEXT("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"session_id\":\"S\",\"num_turns\":4}"));
    Final->SetStringField(TEXT("result"), Supplement);
    TArray<TSharedPtr<FJsonValue>> Stream = {MakeShared<FJsonValueObject>(First), MakeShared<FJsonValueObject>(Continue),
        MakeShared<FJsonValueObject>(Compact), MakeShared<FJsonValueObject>(Summary), MakeShared<FJsonValueObject>(Thinking), MakeShared<FJsonValueObject>(Last)};
    auto Parse = [&]() { return KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream, 3); };
    const auto Recovered = Parse();
    TestEqual(TEXT("Complete draft and final supplement both survive"), Recovered.Content, Body + TEXT("\n\n") + Supplement);
    TestEqual(TEXT("Archive receives identical full content"), Recovered.Assistant->GetStringField(TEXT("content")), Recovered.Content);
    TestEqual(TEXT("Recovery does not alter recorded model calls"), Recovered.Attempts, 4);
    TestFalse(TEXT("Neither incomplete review nor private blocks become visible"), Recovered.Content.Contains(TEXT("<khu_review>")) || Recovered.Content.Contains(TEXT("PRIVATE_")));
    FKHULearningCoach Coach;
    TestEqual(TEXT("Coach processing preserves restored draft"), Coach.Consume(Recovered.Content, 3, {}, TEXT("")), Recovered.Content);
    TestTrue(TEXT("Missing final review stays missing"), !Coach.Review && Coach.Save()->GetObjectField(TEXT("delivery"))->GetStringField(TEXT("state")) == TEXT("not_provided"));
    TestEqual(TEXT("L0 never combines two visible answers"), KHUClaudeProtocol::Result(Final, Config(), TEXT("fixture"), Stream, 0).Content, Supplement);
    Continue->SetBoolField(TEXT("isSynthetic"), false);
    TestEqual(TEXT("Real user with identical words breaks recovery"), Parse().Content, Supplement); Continue->SetBoolField(TEXT("isSynthetic"), true);
    Continue->SetBoolField(TEXT("isReplay"), true);
    TestEqual(TEXT("Replay cannot contribute a previous answer"), Parse().Content, Supplement); Continue->SetBoolField(TEXT("isReplay"), false);
    Continue->GetObjectField(TEXT("message"))->SetStringField(TEXT("content"), TEXT("An unrelated synthetic notification"));
    TestEqual(TEXT("Unrelated synthetic message breaks recovery"), Parse().Content, Supplement);
    Continue->GetObjectField(TEXT("message"))->SetStringField(TEXT("content"), TEXT("Output token limit hit. Resume directly — no apology, no recap of what you were doing. Pick up mid-thought if that is where the cut happened. Break remaining work into smaller pieces."));
    Compact->SetStringField(TEXT("session_id"), TEXT("FOREIGN"));
    TestEqual(TEXT("Foreign compaction breaks recovery"), Parse().Content, Supplement); Compact->SetStringField(TEXT("session_id"), TEXT("S"));
    Summary->SetBoolField(TEXT("isSynthetic"), false);
    TestEqual(TEXT("Real user after compaction is still a boundary"), Parse().Content, Supplement); Summary->SetBoolField(TEXT("isSynthetic"), true);
    Summary->GetObjectField(TEXT("message"))->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Json(TEXT("{\"type\":\"tool_result\",\"content\":\"not an answer\"}")))});
    TestEqual(TEXT("Synthetic tool results cannot impersonate a summary"), Parse().Content, Supplement);
    Summary->GetObjectField(TEXT("message"))->SetStringField(TEXT("content"), TEXT("PRIVATE_COMPACT_SUMMARY"));
    Last->SetStringField(TEXT("parent_tool_use_id"), TEXT("nested"));
    TestEqual(TEXT("Nested final message cannot validate the result"), Parse().Content, Supplement); Last->SetField(TEXT("parent_tool_use_id"), MakeShared<FJsonValueNull>());
    Last->GetObjectField(TEXT("message"))->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Json(TEXT("{\"type\":\"tool_use\",\"name\":\"Read\"}")))});
    TestEqual(TEXT("Tool-use message clears the candidate"), Parse().Content, Supplement);
    Last = Assistant(Supplement + Trailer, TEXT("")); Stream.Last() = MakeShared<FJsonValueObject>(Last); Final->SetStringField(TEXT("result"), Supplement + Trailer);
    TestEqual(TEXT("Final metadata is retained exactly once"), Parse().Content, Body + TEXT("\n\n") + Supplement + Trailer);
    Last = Assistant(Body + TEXT("\n\n") + Supplement, TEXT("")); Stream.Last() = MakeShared<FJsonValueObject>(Last); Final->SetStringField(TEXT("result"), Body + TEXT("\n\n") + Supplement);
    TestEqual(TEXT("Complete replacement that repeats the draft is not duplicated"), Parse().Content, Body + TEXT("\n\n") + Supplement);
    const FString Rewritten = TEXT("수정한 학생 확인 전 AI 초안\n문제: 소수 비율 표현.\n선택 이유: 소수 부분 보존.\n기여: 학생 선택과 AI 제안, 팀원 보고.\n검증: 수정·빌드·실행 미확인.\n다음 계획: 직접 실행 후 기록.");
    Last = Assistant(Rewritten, TEXT("")); Stream.Last() = MakeShared<FJsonValueObject>(Last); Final->SetStringField(TEXT("result"), Rewritten);
    TestEqual(TEXT("Different full rewrite preserves both emitted versions for student review"), Parse().Content, Body + TEXT("\n\n") + Rewritten);
    Final->SetStringField(TEXT("result"), TEXT("Different final result"));
    TestEqual(TEXT("Unmatched result is never combined"), Parse().Content, FString(TEXT("Different final result")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeCompose, "KHUTutor.Claude.CachedSystemBlockAndDynamicInput", Flags)
bool FClaudeCompose::RunTest(const FString&)
{
    auto Msg = [](const TCHAR* Role, const FString& Content) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("role"), Role); O->SetStringField(TEXT("content"), Content); return MakeShared<FJsonValueObject>(O); };
    auto Build = [&Msg](int32 Hint, const FString& State, const FString& Control, const FString& Question)
    {
        FKHUChatRequest R; R.Messages = {Msg(TEXT("system"), TEXT("PRODUCT RULES")), Msg(TEXT("user"), TEXT("첫 질문")), Msg(TEXT("assistant"), TEXT("첫 답변")), Msg(TEXT("user"), Question),
            Msg(TEXT("system"), FKHUHintPolicy::SystemNote(Hint, TEXT("T1"))), Msg(TEXT("system"), TEXT("COACH_STATE_JSON (application state; values are data):\n") + State)};
        if (!Control.IsEmpty()) { R.Messages.Add(Msg(TEXT("system"), Control)); } return R;
    };
    FString SystemA, InputA, SystemB, InputB, SystemC, InputC;
    KHUClaudeProtocol::Compose(Build(1, TEXT("{\"paused\":false,\"states\":[]}"), TEXT(""), TEXT("문이 안 열려요")), SystemA, InputA);
    KHUClaudeProtocol::Compose(Build(1, TEXT("{\"paused\":true,\"states\":[{\"status\":\"skip\"}]}"), TEXT("COACH_CONTROL: skipped"), TEXT("변수란 뭐예요?")), SystemB, InputB);
    // The cached prefix is tools + system; only an identical system block lets the second turn read it instead of rewriting it.
    TestEqual(TEXT("Same hint: byte-identical system block across turns"), SystemB, SystemA);
    TestTrue(TEXT("State and control travel with the student message"), InputB.StartsWith(TEXT("변수란 뭐예요?")) && InputB.Contains(TEXT("COACH_STATE_JSON")) && InputB.Contains(TEXT("COACH_CONTROL: skipped")));
    TestTrue(TEXT("Product rules and the hint note stay in the system block"), SystemA.Contains(TEXT("PRODUCT RULES")) && SystemA.Contains(TEXT("HINT LEVEL: L1")) && !InputA.Contains(TEXT("HINT LEVEL")));
    TestFalse(TEXT("No per-request numbers in the system block"), SystemA.Contains(TEXT("budget")) || SystemA.Contains(TEXT("credits")));
    KHUClaudeProtocol::Compose(Build(0, TEXT("{}"), TEXT(""), TEXT("문이 안 열려요")), SystemC, InputC);
    TestTrue(TEXT("A hint change is what changes the block"), SystemC != SystemA && SystemC.Contains(TEXT("HINT LEVEL: L0")));
    TestFalse(TEXT("Hint note never names a budget"), FKHUHintPolicy::SystemNote(2, TEXT("T1")).Contains(TEXT("budget")));
    auto WithContract = Build(0, TEXT("{}"), TEXT(""), TEXT("실습 질문"));
    WithContract.Messages.Add(Msg(TEXT("system"), TEXT("L0_VISIBLE_CONTRACT: one question, no examples")));
    FString SystemD, InputD; KHUClaudeProtocol::Compose(WithContract, SystemD, InputD);
    TestEqual(TEXT("The final visible-answer reminder does not alter the cached system"), SystemD, SystemC);
    TestTrue(TEXT("Visible-answer reminder follows current application state"), InputD.EndsWith(TEXT("L0_VISIBLE_CONTRACT: one question, no examples")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeHistory, "KHUTutor.Claude.HistoryFolderScopeAndHint", Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeConnection, "KHUTutor.Claude.ConnectionAndExactToolSet", Flags)
bool FClaudeConnection::RunTest(const FString&)
{
    TArray<TSharedPtr<FJsonValue>> Schemas = {MakeShared<FJsonValueObject>(Json(TEXT("{\"name\":\"Ping\"}"))), MakeShared<FJsonValueObject>(Json(TEXT("{\"name\":\"SearchLearningHistory\"}")))};
    auto Init = Json(TEXT("{\"mcp_servers\":[{\"name\":\"khu-tutor\",\"status\":\"connected\"}],\"tools\":[\"mcp__khu-tutor__Ping\",\"mcp__khu-tutor__SearchLearningHistory\"]}"));
    const auto Valid = KHUClaudeProtocol::RuntimeInfo(Init, TEXT("session-fixture"), true, Schemas);
    TestTrue(TEXT("Exact connection and tools accepted"), Valid->GetBoolField(TEXT("mcp_connected")) && Valid->GetBoolField(TEXT("only_tutor_tools")));
    TestTrue(TEXT("Resume attribution retained"), Valid->GetStringField(TEXT("session_id")) == TEXT("session-fixture") && Valid->GetBoolField(TEXT("resumed")));
    const auto Missing = KHUClaudeProtocol::RuntimeInfo(nullptr, TEXT("session-fixture"), false, Schemas);
    TestFalse(TEXT("Missing initialization is disconnected"), Missing->GetBoolField(TEXT("mcp_connected"))); TestFalse(TEXT("Missing initialization cannot verify tools"), Missing->GetBoolField(TEXT("only_tutor_tools")));
    Init->GetArrayField(TEXT("mcp_servers"))[0]->AsObject()->SetStringField(TEXT("status"), TEXT("failed"));
    TestFalse(TEXT("Failed server cannot pass connection"), KHUClaudeProtocol::RuntimeInfo(Init, TEXT("s"), false, Schemas)->GetBoolField(TEXT("mcp_connected")));
    for (const TCHAR* Invalid : {TEXT("Bash"), TEXT("mcp__khu-tutor__Unexpected"), TEXT("mcp__khu-tutor__Ping")})
    {
        Init->SetArrayField(TEXT("tools"), {MakeShared<FJsonValueString>(TEXT("mcp__khu-tutor__Ping")), MakeShared<FJsonValueString>(Invalid)});
        TestFalse(TEXT("Unexpected or duplicate tool rejected even at expected count"), KHUClaudeProtocol::RuntimeInfo(Init, TEXT("s"), false, Schemas)->GetBoolField(TEXT("only_tutor_tools")));
    }
    Init->SetArrayField(TEXT("tools"), {MakeShared<FJsonValueString>(TEXT("mcp__khu-tutor__Ping"))});
    TestFalse(TEXT("Incomplete tool list rejected"), KHUClaudeProtocol::RuntimeInfo(Init, TEXT("s"), false, Schemas)->GetBoolField(TEXT("only_tutor_tools")));
    Init->SetArrayField(TEXT("tools"), {MakeShared<FJsonValueString>(TEXT("mcp__khu-tutor__Ping")), MakeShared<FJsonValueNumber>(1)});
    TestFalse(TEXT("Malformed tool value rejected"), KHUClaudeProtocol::RuntimeInfo(Init, TEXT("s"), false, Schemas)->GetBoolField(TEXT("only_tutor_tools"))); return true;
}
bool FClaudeHistory::RunTest(const FString&)
{
    auto C = Config(); FKHUProjectMemory M(File(), TEXT("P"), C.Context); FString Folder, Other, Id, Target, Foreign, WrongScope;
    M.CreateFolder(TEXT("문 만들기"), Folder); M.CreateFolder(TEXT("캐릭터 이동"), Other);
    M.CreateConversation(TEXT("/Game/Door"), Folder, 3, Id); M.CreateConversation(TEXT("/Game/Door"), Folder, 3, Target); M.CreateConversation(TEXT("/Game/Door"), Other, 3, Foreign); M.CreateConversation(TEXT("/Game/Character"), Folder, 3, WrongScope);
    for (const auto& Chat : {Target, Foreign, WrongScope}) { M.RecordTurn(Chat, Chat == WrongScope ? TEXT("/Game/Character") : TEXT("/Game/Door"), TEXT("문 학습"), TEXT("user"), TEXT("DoorTarget을 Door A로 결정했다"), TEXT("u1"), 3, 1); }
    M.RecordTurn(Target, TEXT("/Game/Door"), TEXT("문 학습"), TEXT("assistant"), TEXT("L3_SOLUTION_SECRET"), TEXT("a1"), 3, 1);
    FKHUChatRequest R; R.TaskId = Id; R.AssetScope = TEXT("/Game/Door"); R.Hint = 1;
    const auto Search = Json(KHUClaudeProtocol::History(M, R, TEXT("SearchLearningHistory"), Json(TEXT("{\"query\":\"DoorTarget\"}"))));
    TestEqual(TEXT("Only matching folder and scope"), Search->GetArrayField(TEXT("items")).Num(), 1);
    auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("conversationId"), Target); A->SetNumberField(TEXT("offset"), 0);
    const FString Read = KHUClaudeProtocol::History(M, R, TEXT("ReadLearningConversation"), A);
    TestTrue(TEXT("Student decision remains readable"), Read.Contains(TEXT("Door A"))); TestFalse(TEXT("Higher hint solution hidden"), Read.Contains(TEXT("L3_SOLUTION_SECRET")));
    A->SetStringField(TEXT("conversationId"), Foreign); TestTrue(TEXT("Cross-folder direct id cannot bypass scope"), Json(KHUClaudeProtocol::History(M, R, TEXT("ReadLearningConversation"), A))->GetArrayField(TEXT("items")).IsEmpty());
    M.EditConversation(Id, TEXT("문"), Folder, true); TestTrue(TEXT("Archived active conversation fails closed"), KHUClaudeProtocol::History(M, R, TEXT("SearchLearningHistory"), Json(TEXT("{\"query\":\"\"}"))).StartsWith(TEXT("ERROR"))); M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudePolicy, "KHUTutor.Claude.StudentModelPolicy", Flags)
bool FClaudePolicy::RunTest(const FString&)
{
    auto C = Config(); TestTrue(TEXT("Runtime configuration valid"), C.bValid && C.bClaudePanel);
    TestTrue(TEXT("L0 visible-answer contract loads from Config/Prompts"), C.L0Contract.Contains(TEXT("질문 한 문장")) && !C.L0Contract.StartsWith(TEXT("L0_VISIBLE_CONTRACT")));
    for (const FString Tier : {FString(TEXT("T1")), FString(TEXT("T2"))}) { for (const auto& Model : C.Tiers[Tier].Chain) { TestTrue(TEXT("Student tiers Claude only"), Model.StartsWith(TEXT("claude-")) && !Model.StartsWith(TEXT("claude-opus-"))); } }
    TestFalse(TEXT("Opus is not enabled for students"), C.IsModelEnabled(C.Tiers[TEXT("D7")].Chain[0]));
    auto Runtime = MakeShared<FKHUClaudeRuntime>(C, TEXT("fixture-not-a-key")); TestTrue(TEXT("Managed history boundary"), Runtime->OwnsHistory()); Runtime->Cancel(); Runtime->Cancel(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeInstructionFiles, "KHUTutor.Claude.SubscriptionWorkspaceInstructionCheck", Flags)
bool FClaudeInstructionFiles::RunTest(const FString&)
{
    // The development subscription path drops --bare, so any CLAUDE.md above its workspace would reach the student model.
    const FString Base = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KHUTutor/ClaudeTests") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString Deep = Base / TEXT("a/b/workspace"); IFileManager::Get().MakeDirectory(*Deep, true); FString Found;
    const FString Repository = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../CLAUDE.md"));
    if (FPaths::FileExists(Repository)) { TestTrue(TEXT("Repository CLAUDE.md is detected from inside the project"), FKHUClaudeRuntime::FindInstructionFile(Deep, Found)); }
    for (const FString Name : {FString(TEXT("CLAUDE.md")), FString(TEXT("CLAUDE.local.md")), FString(TEXT(".claude/CLAUDE.md"))})
    {
        const FString Planted = Base / TEXT("a") / Name; FFileHelper::SaveStringToFile(TEXT("canary"), *Planted);
        Found.Reset(); TestTrue(TEXT("Parent instruction file found"), FKHUClaudeRuntime::FindInstructionFile(Deep, Found));
        TestEqual(TEXT("Nearest planted file reported"), FPaths::ConvertRelativePathToFull(Found), FPaths::ConvertRelativePathToFull(Planted)); IFileManager::Get().Delete(*Planted);
    }
    TestEqual(TEXT("Subscription variable is development-specific"), FString(FKHUClaudeRuntime::SubscriptionTokenVariable()), FString(TEXT("KHU_DEV_CLAUDE_OAUTH_TOKEN")));
    IFileManager::Get().DeleteDirectory(*Base, false, true); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudePages, "KHUTutor.Claude.OriginalMessagePagination", Flags)
bool FClaudePages::RunTest(const FString&)
{
    auto C = Config(); FKHUProjectMemory M(File(), TEXT("P"), C.Context); FString Id; M.CreateConversation(TEXT(""), TEXT(""), 1, Id);
    const FString Original = FString::ChrN(13500, TCHAR(0xAC00)) + TEXT("LAST_DECISION_DOOR_A");
    TestTrue(TEXT("Long original stored"), M.RecordTurn(Id, TEXT(""), TEXT("긴 기록"), TEXT("user"), Original, TEXT("u1"), 1, 1));
    FKHUChatRequest R; R.TaskId = Id; R.Hint = 1; auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("conversationId"), Id); A->SetNumberField(TEXT("offset"), 0); A->SetNumberField(TEXT("characterOffset"), 0);
    FString Joined; bool More = true; int32 Pages = 0;
    while (More && Pages++ < 5)
    {
        const auto O = Json(KHUClaudeProtocol::History(M, R, TEXT("ReadLearningConversation"), A)); int32 Size = 0;
        for (const auto& V : O->GetArrayField(TEXT("items"))) { const FString Text = V->AsObject()->GetStringField(TEXT("text")); Size += Text.Len(); Joined += Text; }
        TestTrue(TEXT("Every page is bounded"), Size <= 6000); More = O->GetBoolField(TEXT("has_more")); A->SetNumberField(TEXT("offset"), O->GetNumberField(TEXT("next_offset"))); A->SetNumberField(TEXT("characterOffset"), O->GetNumberField(TEXT("next_character_offset")));
    }
    TestFalse(TEXT("Complete after three pages"), More); TestEqual(TEXT("All original text including late decision recoverable"), Joined, Original); M.Forget(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClaudeLive, "KHUTutor.Claude.Live.ReopenAndOriginalLookup", Flags)
bool FClaudeLive::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorClaudeLive"))) { AddInfo(TEXT("Paid Claude panel runtime test not requested")); return true; }
    FString Error, Key; if (!FKHUSecretStore(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi")).Load(Key, Error)) { AddError(Error); return false; }
    const auto C = Config(); const FString Path = File(); const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    auto Memory = MakeShared<FKHUProjectMemory>(Path, Project, C.Context); FString Folder, Old, Id;
    if (!Memory->CreateFolder(TEXT("검증 · Claude 이어받기"), Folder) || !Memory->CreateConversation(TEXT(""), Folder, 1, Old) || !Memory->CreateConversation(TEXT(""), Folder, 1, Id)) { AddError(Memory->Error()); Memory->Forget(); return false; }
    Memory->RecordTurn(Old, TEXT(""), TEXT("문 만들기"), TEXT("user"), TEXT("합성 검증 기록: 문 개폐 방식은 타임라인으로 결정했고, 이름은 문실험-725이다. 실제 자산이나 학생 기록이 아니다."), TEXT("u1"), 1, 1);
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FCheck(FAutomationTestBase* InTest, FKHUConfig InConfig, FString InKey, TSharedRef<FKHUProjectMemory> InMemory, FString InId)
            : Test(InTest), C(MoveTemp(InConfig)), Key(MoveTemp(InKey)), Memory(InMemory), Id(MoveTemp(InId)), Deadline(FPlatformTime::Seconds() + 500) { Start(); }
        ~FCheck() override { if (Agent) { Agent->Cancel(); } Memory->Forget(); }
        void Start()
        {
            auto Client = MakeShared<FKHUClaudeRuntime>(C, Key, Memory); Agent = MakeShared<FKHUAgentLoop>(C, Client, MakeShared<FKHUToolExecutorDirect>(), MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog())); Agent->SetProjectMemory(Memory, TEXT(""));
            const FString Prompt = Stage == 0 ? TEXT("합성 학습 검증입니다. 목표는 스위치로 Door A 열기. DoorTarget은 Door A로 정했고 T 입력으로 테스트하기로 했어요. 아직 Play로 검증하지 않았어요. 먼저 Ping을 한 번 호출해서 프로젝트 이름을 확인하고, 이 목표·결정·미확인 상태를 짧게 정리해 주세요.")
                : Stage == 1 ? TEXT("대화를 다시 열었어요. 우리가 어떤 문을 어떤 변수로 조작하기로 했고, 어떤 키로 테스트하며, 실제 게임 검증을 마쳤는지 기억해서 알려 주세요. 현재 상태를 확인했다고 주장하지 마세요.")
                : TEXT("같은 학습 폴더의 이전 대화에서 문 개폐 방식을 정했었어요. SearchLearningHistory로 문실험-725 기록을 찾고 ReadLearningConversation으로 원문을 읽어 그 방식을 알려 주세요. 지금 에디터를 변경하지 마세요.");
            const bool Started = Agent->Ask(Prompt, TEXT("T1"), 1, [](EKHUAgentState, const FString&) {}, true, false, Id); Test->TestTrue(TEXT("Actual native Claude run started"), Started); if (!Started) { Done = true; }
        }
        virtual bool Update() override
        {
            if (Done) { return true; } if (FPlatformTime::Seconds() > Deadline) { Agent->Cancel(); Test->AddError(TEXT("Claude live timeout")); return true; }
            if (Agent->State() != EKHUAgentState::Idle && Agent->State() != EKHUAgentState::AskContinue) { return false; }
            const auto R = Agent->Receipt(); const FString Text = R->GetStringField(TEXT("text")); const auto& Tools = R->GetArrayField(TEXT("tools")); const auto& Usage = R->GetArrayField(TEXT("usage"));
            bool Success = Agent->State() == EKHUAgentState::Idle && Usage.Num() == 1 && Usage[0]->AsObject()->GetBoolField(TEXT("success"));
            if (Usage.Num() == 1)
            { const TSharedPtr<FJsonObject>* Runtime = nullptr; Success &= Usage[0]->AsObject()->TryGetObjectField(TEXT("runtime"), Runtime);
              if (Runtime) { const FString Session = (*Runtime)->GetStringField(TEXT("session_id")); if (Stage == 0) { SessionId = Session; } Success &= Session == SessionId && (*Runtime)->GetBoolField(TEXT("resumed")) == (Stage > 0) && (*Runtime)->GetBoolField(TEXT("mcp_connected")) && (*Runtime)->GetBoolField(TEXT("only_tutor_tools")); } }
            if (Stage == 0) { Success &= Tools.Num() >= 1 && Text.Contains(TEXT("TutorSandbox")); }
            if (Stage == 1) { Success &= Text.Contains(TEXT("Door A")) && Text.Contains(TEXT("DoorTarget")) && Text.Contains(TEXT("T")) && (Text.Contains(TEXT("아직")) || Text.Contains(TEXT("미확인"))); }
            if (Stage == 2) { bool Search = false, Read = false; for (const auto& V : Tools) { Search |= V->AsObject()->GetStringField(TEXT("native")) == TEXT("SearchLearningHistory"); Read |= V->AsObject()->GetStringField(TEXT("native")) == TEXT("ReadLearningConversation"); } Success &= Search && Read && Text.Contains(TEXT("타임라인")); }
            Test->TestTrue(TEXT("Actual answer and required tool evidence"), Success); const FString Output = FPaths::ProjectSavedDir() / TEXT("KHUTutor") / FString::Printf(TEXT("claude_panel_live_%d.json"), Stage);
            Test->TestTrue(TEXT("Evidence saved"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(R), *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            Agent->Cancel(); Agent.Reset(); if (!Success || ++Stage >= 3) { return true; } Start(); return false;
        }
    private:
        FAutomationTestBase* Test; FKHUConfig C; FString Key; TSharedRef<FKHUProjectMemory> Memory; FString Id, SessionId; TSharedPtr<FKHUAgentLoop> Agent; int32 Stage = 0; double Deadline; bool Done = false;
    };
    AddCommand(new FCheck(this, C, MoveTemp(Key), Memory, Id)); return true;
}
#endif

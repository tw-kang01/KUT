#include "AI/KHULearningCoach.h"
#include "AI/KHUProjectMemory.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/Guid.h"
#include <initializer_list>

namespace
{
    bool In(const FString& S, std::initializer_list<const TCHAR*> Values)
    { for (const auto* V : Values) { if (S == V) { return true; } } return false; }
    bool Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K, FString& S, int32 Max = 240)
    { return O && O->TryGetStringField(K, S) && S.Len() <= Max; }
    bool Exact(const TSharedPtr<FJsonObject>& O, std::initializer_list<const TCHAR*> Keys)
    { if (!O || O->Values.Num() != static_cast<int32>(Keys.size())) { return false; } for (const auto* K : Keys) { if (!O->HasField(K)) { return false; } } return true; }
    bool Evidence(const TSharedPtr<FJsonObject>& O, const TArray<FKHUCoachSource>& Sources, FKHUCoachSource& Source)
    {
        FString Id, Quote;
        if (!Str(O, TEXT("source"), Id, 128) || !Str(O, TEXT("quote"), Quote) || Quote.TrimStartAndEnd().IsEmpty()) { return false; }
        const auto* S = Sources.FindByPredicate([&](const auto& V) { return V.Id == Id; });
        if (!S || !S->Text.Contains(Quote, ESearchCase::CaseSensitive)) { return false; } Source = *S; return true;
    }
    bool Shape(const TSharedPtr<FJsonObject>& O)
    {
        FString Stage, Action, Trigger, Report, Source, Quote;
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Exact(O, {TEXT("stage"), TEXT("action"), TEXT("trigger"), TEXT("report"), TEXT("source"), TEXT("quote"), TEXT("items")})
            || !Str(O, TEXT("stage"), Stage) || !In(Stage, {TEXT("define"), TEXT("predict"), TEXT("choose"), TEXT("verify")})
            || !Str(O, TEXT("action"), Action) || !In(Action, {TEXT("none"), TEXT("predict"), TEXT("choose"), TEXT("verify")})
            || !Str(O, TEXT("trigger"), Trigger) || !In(Trigger, {TEXT("none"), TEXT("observation"), TEXT("decision"), TEXT("request")})
            || !Str(O, TEXT("report"), Report) || !In(Report, {TEXT("none"), TEXT("attempt"), TEXT("verification")})
            || !Str(O, TEXT("source"), Source, 128) || !Str(O, TEXT("quote"), Quote)
            || !O->TryGetArrayField(TEXT("items"), Items) || Items->IsEmpty() || Items->Num() > 5) { return false; }
        TSet<FString> Seen; int32 Improvements = 0;
        for (const auto& V : *Items)
        {
            if (!V || V->Type != EJson::Object) { return false; } const auto I = V->AsObject(); FString Item, Status;
            if (!Exact(I, {TEXT("item"), TEXT("status"), TEXT("source"), TEXT("quote")})
                || !Str(I, TEXT("item"), Item) || !In(Item, {TEXT("goal"), TEXT("observation"), TEXT("thinking"), TEXT("delegation"), TEXT("verification")})
                || Seen.Contains(Item) || !Str(I, TEXT("status"), Status) || !In(Status, {TEXT("supported"), TEXT("improve"), TEXT("na"), TEXT("unknown")})
                || !Str(I, TEXT("source"), Source, 128) || !Str(I, TEXT("quote"), Quote)) { return false; } Seen.Add(Item); if (Status == TEXT("improve") && ++Improvements > 1) { return false; }
        }
        return true;
    }
    TSharedRef<FJsonObject> Copy(const TSharedPtr<FJsonObject>& O)
    { auto R = MakeShared<FJsonObject>(); R->Values = O->Values; return R; }
    bool HasBodyAction(const FString& Body)
    {
        // Conservative delivery guard: retain the complete answer and omit only the extra card task.
        // Code and quoted blocks are not student-facing instructions. This is not semantic grading.
        TArray<FString> Lines; Body.ParseIntoArrayLines(Lines); bool Fence = false;
        for (auto Line : Lines)
        {
            Line.TrimStartAndEndInline();
            if (Line.StartsWith(TEXT("```")) || Line.StartsWith(TEXT("~~~"))) { Fence = !Fence; continue; }
            if (Fence || Line.StartsWith(TEXT(">"))) { continue; }
            Line.ReplaceInline(TEXT(" "), TEXT("")); Line.ReplaceInline(TEXT("\t"), TEXT(""));
            for (const TCHAR* Cue : {TEXT("예상해보세요"), TEXT("예측해보세요"), TEXT("적어보세요"), TEXT("적어주세요"),
                TEXT("비교해보세요"), TEXT("설명해보세요"), TEXT("골라보세요"), TEXT("선택해보세요"),
                TEXT("확인해보세요"), TEXT("실행해보세요"), TEXT("테스트해보세요"), TEXT("생각해보세요")})
            { if (Line.Contains(Cue)) { return true; } }
        }
        return false;
    }
    // Review wording is application-owned: the model selects an item, a status and an exact source quote only.
    struct FItemText { const TCHAR* Item; const TCHAR* Label; const TCHAR* Helped; const TCHAR* Improve; const TCHAR* Example; };
    const FItemText& ItemText(const FString& Item)
    {
        static const FItemText Texts[] = {
            {TEXT("goal"), TEXT("목표"), TEXT("무엇을 원하는지 알 수 있어서 필요한 도움의 범위를 정했어요."), TEXT("만들거나 고치려는 결과를 한 문장으로 정해 보세요. 목표가 분명하면 필요한 도움만 받을 수 있어요."), TEXT("“___을 하면 ___이 되게 하고 싶어요.”")},
            {TEXT("observation"), TEXT("관찰"), TEXT("실제로 본 내용이 있어서 추측을 줄일 수 있었어요."), TEXT("기대한 결과와 실제로 본 결과를 하나씩 적어 보세요. 직접 본 내용이 있어야 추측 대신 근거로 원인을 좁힐 수 있어요."), TEXT("“___을 기대했는데 실제로는 ___였어요.”")},
            {TEXT("thinking"), TEXT("현재 생각·시도"), TEXT("지금 생각을 알려 줘서 그 지점부터 도울 수 있었어요. 생각이 맞는지는 따로 확인해요."), TEXT("지금 떠오르는 생각이나 해 본 것을 하나만 적어 보세요. 틀려도 괜찮고, 어디부터 모르는지 적어도 돼요."), TEXT("“___ 때문인 것 같아요.” 또는 “___부터 모르겠어요.”")},
            {TEXT("delegation"), TEXT("AI에게 맡길 범위"), TEXT("AI가 도울 범위가 분명해서 필요한 만큼만 도울 수 있었어요."), TEXT("AI에게 맡길 부분과 직접 해 볼 부분을 나눠 보세요. 직접 판단한 부분이 남아야 나중에 스스로 설명할 수 있어요."), TEXT("“___은 설명해 주고, ___은 제가 해 볼게요.”")},
            {TEXT("verification"), TEXT("검증 기준"), TEXT("무엇으로 판단할지 기준이 있어서 결과를 확인할 수 있어요."), TEXT("어떤 결과가 나오면 맞다고 볼지 기준 하나를 정해 보세요. 기준이 있어야 AI의 제안도 직접 판단할 수 있어요."), TEXT("“___을 했을 때 ___이 되면 맞아요.”")}};
        for (const auto& T : Texts) { if (Item == T.Item) { return T; } }
        return Texts[0]; // Unreachable: Shape() admits only the five items above before anything is displayed.
    }
}
FString FKHULearningCoach::Consume(const FString& Raw, int32 Hint, const TArray<FKHUCoachSource>& Sources, const FString& PriorAI)
{
    Review.Reset(); Delivery = TEXT("not_provided"); DeliveryHint = Hint; DeliveryId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const int32 At = Raw.Find(TEXT("<khu_review"), ESearchCase::IgnoreCase);
    if (At == INDEX_NONE) { return Raw; }
    Delivery = TEXT("invalid");
    FString Body = Raw.Left(At).TrimStartAndEnd();
    FString Tail = Raw.Mid(At).TrimEnd();
    // A fenced trailer is still metadata; its fence must not stay in the visible answer.
    if (Tail.EndsWith(TEXT("```")))
    {
        Tail = Tail.LeftChop(3).TrimEnd();
        for (const TCHAR* Fence : {TEXT("```json"), TEXT("```")}) { if (Body.EndsWith(Fence)) { Body = Body.LeftChop(FCString::Strlen(Fence)).TrimEnd(); break; } }
    }
    if (!Tail.StartsWith(TEXT("<khu_review>")) || !Tail.EndsWith(TEXT("</khu_review>"))) { return Body; }
    const int32 End = Tail.Find(TEXT("</khu_review>"));
    if (End < 12 || End > 6000 || Tail.Mid(End + 13).TrimStartAndEnd().Len() > 0) { return Body; }
    TSharedPtr<FJsonObject> O;
    if (!KHUMcpProtocol::ReadObject(Tail.Mid(12, End - 12), O) || !Shape(O)) { return Body; }
    FKHUCoachSource Main;
    if (!Evidence(O, Sources, Main)) { return Body; }
    for (const auto& V : O->GetArrayField(TEXT("items")))
    { FKHUCoachSource S; if (!Evidence(V->AsObject(), Sources, S)) { return Body; } }
    const FString Quote = O->GetStringField(TEXT("quote"));
    const FString Trigger = O->GetStringField(TEXT("trigger"));
    const FString Stage = O->GetStringField(TEXT("stage"));
    const bool CopiedAI = PriorAI.Contains(Quote, ESearchCase::CaseSensitive);
    // Explicitly hypothetical text must never become a performed-check event, even if the model
    // labels it verification. Conservative omission affects the record only, not ordinary help.
    bool Hypothetical = false;
    if (Main.Kind == TEXT("student"))
    {
        for (const TCHAR* Marker : {TEXT("가정합니다"), TEXT("가정하면"), TEXT("가정해"), TEXT("가정하"), TEXT("hypothetical"), TEXT("suppose ")})
        { Hypothetical |= Main.Text.Contains(Marker, ESearchCase::IgnoreCase); }
    }
    // A retrospective request quotes old reports; it is not another performed action.
    const bool Retrospective = Main.Kind == TEXT("student") && Main.Text.StartsWith(TEXT("아래는 과거 대화에서 학생이 고른 문제 해결 기록 데이터입니다."));
    const bool NotCurrentReport = Hypothetical || Retrospective;
    const bool SeenQuote = Events.ContainsByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("quote")) == Quote; });
    const bool NewEvidence = Main.bCurrent && !CopiedAI && !NotCurrentReport && !SeenQuote && Trigger != TEXT("none");
    const bool Already = Events.ContainsByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("stage")) == Stage; });
    auto R = Copy(O);
    R->SetStringField(TEXT("quote"), FKHUProjectMemory::Excerpt(Quote, 240));
    TArray<TSharedPtr<FJsonValue>> SafeItems;
    for (const auto& V : O->GetArrayField(TEXT("items")))
    { auto I = Copy(V->AsObject()); I->SetStringField(TEXT("quote"), FKHUProjectMemory::Excerpt(I->GetStringField(TEXT("quote")), 240)); SafeItems.Add(MakeShared<FJsonValueObject>(I)); }
    R->SetArrayField(TEXT("items"), SafeItems); R->SetNumberField(TEXT("hint"), Hint);
    R->SetStringField(TEXT("id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    R->SetStringField(TEXT("status"), TEXT("review"));
    // A new message or a different spelling of the same suggestion is not a stage reset.
    const bool AnyProposal = Events.ContainsByPredicate([](const auto& V) { return V->AsObject()->GetStringField(TEXT("status")) == TEXT("proposed"); });
    const bool L0Question = Hint == 0 && (Body.Contains(TEXT("?")) || Body.Contains(TEXT("？")));
    if (bPaused || L0Question || HasBodyAction(Body) || Events.Num() >= 63 || (AnyProposal && !NewEvidence) || (Already && !NewEvidence)) { R->SetStringField(TEXT("action"), TEXT("none")); }
    if (R->GetStringField(TEXT("action")) != TEXT("none")) { R->SetStringField(TEXT("status"), TEXT("proposed")); }
    FString Report = O->GetStringField(TEXT("report"));
    if (NotCurrentReport) { Report = TEXT("none"); R->SetStringField(TEXT("report"), Report); }
    // The opening message's symptom is the observation itself, however it is phrased; logging the same words again
    // as a performed check would make the record depend on wording rather than on what the student did.
    bool Symptom = false;
    if (Main.Id == TEXT("u1"))
    {
        for (const auto& V : O->GetArrayField(TEXT("items")))
        { const auto I = V->AsObject(); const FString Seen = I->GetStringField(TEXT("quote"));
          if (I->GetStringField(TEXT("item")) == TEXT("observation") && (Seen.Contains(Quote, ESearchCase::CaseSensitive) || Quote.Contains(Seen, ESearchCase::CaseSensitive))) { Symptom = true; } }
    }
    // Model classifications retain exact source text; never create a student's rationale or mastery score.
    if (Report != TEXT("none") && !Symptom && Main.bCurrent && !CopiedAI && !SeenQuote && Events.Num() < 63)
    {
        auto E = Copy(R); E->SetStringField(TEXT("action"), TEXT("none"));
        E->SetStringField(TEXT("status"), Main.Kind == TEXT("tool") ? TEXT("tool_evidence") : TEXT("student_report"));
        Events.Add(MakeShared<FJsonValueObject>(E));
    }
    if (R->GetStringField(TEXT("action")) != TEXT("none")) { Events.Add(MakeShared<FJsonValueObject>(Copy(R))); }
    Review = R; Delivery.Reset(); DeliveryId.Reset(); return Body;
}
bool FKHULearningCoach::Choose(const FString& Choice)
{
    if (Choice == TEXT("pause") || Choice == TEXT("resume")) { bPaused = Choice == TEXT("pause"); return true; }
    if (!Review || !In(Choice, {TEXT("try"), TEXT("hint"), TEXT("skip")}) || Review->GetStringField(TEXT("status")) != TEXT("proposed") || Events.Num() >= 64) { return false; }
    auto R = Copy(Review); R->SetStringField(TEXT("status"), Choice);
    Review = R; Events.Add(MakeShared<FJsonValueObject>(Copy(R))); return true;
}
TSharedRef<FJsonObject> FKHULearningCoach::Save() const
{
    auto O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("version"), 1); O->SetBoolField(TEXT("paused"), bPaused);
    O->SetArrayField(TEXT("events"), Events); if (Review) { O->SetObjectField(TEXT("review"), Review); }
    else if (!Delivery.IsEmpty())
    { auto D = MakeShared<FJsonObject>(); D->SetStringField(TEXT("state"), Delivery); D->SetStringField(TEXT("id"), DeliveryId); D->SetNumberField(TEXT("hint"), DeliveryHint); O->SetObjectField(TEXT("delivery"), D); }
    return O;
}
bool FKHULearningCoach::Valid(const TSharedPtr<FJsonObject>& O)
{
    double Version; bool Paused; const TArray<TSharedPtr<FJsonValue>>* EventsPtr;
    if (!O || !O->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !O->TryGetBoolField(TEXT("paused"), Paused)
        || !O->TryGetArrayField(TEXT("events"), EventsPtr) || EventsPtr->Num() > 64) { return false; }
    auto Check = [](const TSharedPtr<FJsonObject>& E)
    {
        FString Id, Status; double Hint;
        if (!Str(E, TEXT("id"), Id, 64) || Id.IsEmpty() || !Str(E, TEXT("status"), Status)
            || !In(Status, {TEXT("review"), TEXT("proposed"), TEXT("try"), TEXT("hint"), TEXT("skip"), TEXT("student_report"), TEXT("tool_evidence")})
            || !E->TryGetNumberField(TEXT("hint"), Hint) || Hint < 0 || Hint > 3 || Hint != FMath::FloorToDouble(Hint)) { return false; }
        auto C = Copy(E); C->RemoveField(TEXT("hint")); C->RemoveField(TEXT("id")); C->RemoveField(TEXT("status")); return Shape(C);
    };
    for (const auto& V : *EventsPtr) { if (!V || V->Type != EJson::Object || !Check(V->AsObject())) { return false; } }
    if (O->HasField(TEXT("delivery")))
    {
        const TSharedPtr<FJsonObject>* D; FString State, Id; double Hint;
        if (O->HasField(TEXT("review")) || !O->TryGetObjectField(TEXT("delivery"), D)
            || !Exact(*D, {TEXT("state"), TEXT("id"), TEXT("hint")})
            || !Str(*D, TEXT("state"), State) || !In(State, {TEXT("not_provided"), TEXT("invalid")})
            || !Str(*D, TEXT("id"), Id, 64) || Id.IsEmpty() || !(*D)->TryGetNumberField(TEXT("hint"), Hint)
            || Hint < 0 || Hint > 3 || Hint != FMath::FloorToDouble(Hint)) { return false; }
    }
    const TSharedPtr<FJsonObject>* R;
    return !O->HasField(TEXT("review")) || (O->TryGetObjectField(TEXT("review"), R) && Check(*R));
}
bool FKHULearningCoach::Restore(const TSharedPtr<FJsonObject>& O)
{
    Review.Reset(); Events.Reset(); bPaused = false; Delivery.Reset(); DeliveryId.Reset(); DeliveryHint = 1;
    if (!O) { return true; } if (!Valid(O)) { bPaused = true; return false; }
    bPaused = O->GetBoolField(TEXT("paused")); Events = O->GetArrayField(TEXT("events"));
    const TSharedPtr<FJsonObject>* R; if (O->TryGetObjectField(TEXT("review"), R)) { Review = Copy(*R); }
    const TSharedPtr<FJsonObject>* D;
    if (O->TryGetObjectField(TEXT("delivery"), D))
    { Delivery = (*D)->GetStringField(TEXT("state")); DeliveryId = (*D)->GetStringField(TEXT("id")); DeliveryHint = static_cast<int32>((*D)->GetNumberField(TEXT("hint"))); }
    return true;
}
FString FKHULearningCoach::Control() const
{
    if (bPaused) { return TEXT("paused"); }
    bool Reported = false;
    for (int32 I = Events.Num() - 1; I >= 0; --I)
    {
        const FString Status = Events[I]->AsObject()->GetStringField(TEXT("status"));
        // A newer proposal is a new step; the choice made on an older step no longer describes it.
        if (Status == TEXT("proposed")) { return TEXT("none"); }
        if (In(Status, {TEXT("student_report"), TEXT("tool_evidence")})) { Reported = true; }
        // An actual report answers "try"/"hint"; a skipped step stays skipped until a new step is proposed.
        if (In(Status, {TEXT("try"), TEXT("hint")})) { return Reported ? TEXT("none") : Status; }
        if (Status == TEXT("skip")) { return Status; }
    }
    return TEXT("none");
}
FString FKHULearningCoach::ControlNote() const
{
    const FString C = Control();
    if (C == TEXT("skip")) { return TEXT("COACH_CONTROL: (application state; overrides conversation continuity) The student chose to skip the last optional coaching step. Do not repeat that step, its procedure or its expected result, and do not ask for its outcome. Help with the current message in a different way within the current hint level, for example explain the idea, narrow the question, or offer to read the editor. Use action=none unless this message reports a materially new observation or decision, or explicitly asks for coaching."); }
    if (C == TEXT("paused")) { return TEXT("COACH_CONTROL: (application state) Automatic coaching is paused for this conversation. Answer the current message directly at the current hint level. Add no unsolicited practice, prediction, choice or verification assignment and no quiz, and use action=none. Diagnosis items may still be reported for a task message."); }
    if (C == TEXT("try")) { return TEXT("COACH_CONTROL: (application state) The student said they will try the last coaching step. That is an intention, not a result. Do not repeat the step or assume its outcome; respond to what they actually report."); }
    if (C == TEXT("hint")) { return TEXT("COACH_CONTROL: (application state) The student asked for more help with the last coaching step. Offer a smaller observation or example for it within the CURRENT hint level. Only the student changes the hint level in the app; never raise it yourself."); }
    return TEXT("");
}
FString FKHULearningCoach::Context(int32 Hint) const
{
    auto O = MakeShared<FJsonObject>(); O->SetBoolField(TEXT("paused"), bPaused);
    TArray<TSharedPtr<FJsonValue>> States;
    for (const auto& V : Events)
    {
        const auto E = V->AsObject(); auto S = MakeShared<FJsonObject>();
        for (const TCHAR* K : {TEXT("stage"), TEXT("status"), TEXT("action")}) { S->SetStringField(K, E->GetStringField(K)); }
        // No old quotes in model context, even when the original hint is unchanged.
        States.Add(MakeShared<FJsonValueObject>(S));
    }
    O->SetNumberField(TEXT("current_hint"), Hint); O->SetArrayField(TEXT("states"), States); return KHUMcpProtocol::WriteObject(O);
}
bool FKHULearningCoach::Visible(int32 Hint) const { return Review && Review->GetNumberField(TEXT("hint")) <= Hint; }
FString FKHULearningCoach::Id() const { return Review ? Review->GetStringField(TEXT("id")) : DeliveryId; }
FString FKHULearningCoach::Action(int32 Hint) const
{
    if (!Visible(Hint) || bPaused || Review->GetStringField(TEXT("status")) != TEXT("proposed")) { return TEXT(""); }
    const FString A = Review->GetStringField(TEXT("action"));
    if (A == TEXT("predict")) { return TEXT("직접 실행하기 전에 어떤 결과가 나올지 한 가지 예상해 보세요. 관찰한 결과와 비교하면 생각이 달라진 지점을 찾을 수 있어요."); }
    if (A == TEXT("choose")) { return TEXT("지금 생각한 방법과 다른 방법 중 하나를 고르고 이유를 적어 보세요. 이 조건에서 무엇을 우선했는지 확인하는 연습이에요."); }
    if (A == TEXT("verify")) { return TEXT("확인할 조건 하나를 정해 기대한 결과와 실제 결과를 비교해 보세요. AI의 제안도 직접 확인한 근거가 있어야 판단할 수 있어요."); }
    return TEXT("");
}
FString FKHULearningCoach::Describe(int32 Hint) const
{
    if (!Visible(Hint)) { return TEXT(""); }
    // A student's own words cannot reveal anything new to them; editor/tool text may, so it stays L3-only.
    auto Basis = [Hint](const TSharedPtr<FJsonObject>& O)
    {
        const FString Source = O->GetStringField(TEXT("source")), Quote = FKHUProjectMemory::Excerpt(O->GetStringField(TEXT("quote")), 160);
        if (Source.Len() > 1 && Source[0] == 'u' && Source.Mid(1).IsNumeric()) { return TEXT("내가 쓴 말: “") + Quote + TEXT("”"); }
        return Hint == 3 ? TEXT("현재 관찰 자료: “") + Quote + TEXT("”") : FString(TEXT("현재 관찰 자료"));
    };
    FString Out;
    for (const auto& V : Review->GetArrayField(TEXT("items")))
    {
        const auto I = V->AsObject(); const FString State = I->GetStringField(TEXT("status")); const FItemText& T = ItemText(I->GetStringField(TEXT("item")));
        if (State == TEXT("na")) { Out += FString(T.Label) + TEXT(" · 이번 질문에는 해당 없음\n"); continue; }
        if (State == TEXT("unknown")) { Out += FString(T.Label) + TEXT(" · 판단 근거 부족\n  아직 확인된 내용이 없어서 판단을 보류했어요.\n"); continue; }
        Out += FString(T.Label) + (State == TEXT("supported") ? TEXT(" · 근거 확인\n  ") : TEXT(" · 보완 제안\n  ")) + Basis(I) + TEXT("\n  ");
        Out += State == TEXT("supported") ? FString(T.Helped) + TEXT("\n") : FString(T.Improve) + TEXT("\n  예: ") + T.Example + TEXT("\n");
    }
    // Only a report that was actually recorded is shown, and never as independent verification.
    const FString ReviewId = Id();
    for (const auto& V : Events)
    {
        const auto E = V->AsObject(); const FString Recorded = E->GetStringField(TEXT("status"));
        if (E->GetStringField(TEXT("id")) != ReviewId || !In(Recorded, {TEXT("student_report"), TEXT("tool_evidence")})) { continue; }
        Out += Recorded == TEXT("tool_evidence") ? TEXT("남긴 기록 · 도구가 읽은 근거\n  ") + Basis(E) + TEXT("\n")
            : FString(E->GetStringField(TEXT("report")) == TEXT("verification") ? TEXT("남긴 기록 · 직접 확인한 결과\n  ") : TEXT("남긴 기록 · 직접 해 본 내용\n  ")) + Basis(E) + TEXT("\n  학생이 알려 준 내용이며, 도구로 따로 확인한 결과는 아니에요.\n");
    }
    const FString Status = Review->GetStringField(TEXT("status"));
    if (Status == TEXT("try")) { Out += TEXT("직접 확인하기를 선택했어요. 실제 수행 결과는 아직 보고되지 않았어요."); }
    if (Status == TEXT("hint")) { Out += TEXT("힌트 요청을 선택했어요. 도움 수준은 학생이 직접 선택해요."); }
    if (Status == TEXT("skip")) { Out += TEXT("이번 제안을 건너뛰었어요. 질문과 일반 도움은 계속 이용할 수 있어요."); }
    return Out.TrimEnd();
}
FString FKHULearningCoach::Notice(int32 Hint) const
{
    FString Out;
    if (!Review && !Delivery.IsEmpty())
    {
        if (DeliveryHint > Hint) { Out = TEXT("지금보다 자세한 도움 수준에서 받은 진단 상태라 숨겼어요."); }
        else { Out = Delivery == TEXT("not_provided") ? TEXT("이 답변에는 질문 돌아보기 정보가 없어요. 학생의 이해나 수행 여부를 판단한 결과는 아니에요.")
            : TEXT("이 답변의 질문 돌아보기 정보를 확인하지 못했어요. 본문은 그대로 이용할 수 있어요."); }
    }
    if (Review && !Visible(Hint)) { Out = TEXT("지금보다 자세한 도움 수준에서 만든 진단이라 숨겼어요."); }
    if (bPaused) { Out += FString(Out.IsEmpty() ? TEXT("") : TEXT("\n")) + TEXT("이번 대화의 자동 코칭을 일시 중지했어요. 질문·힌트·일반 도움은 그대로 이용할 수 있어요."); }
    return Out;
}

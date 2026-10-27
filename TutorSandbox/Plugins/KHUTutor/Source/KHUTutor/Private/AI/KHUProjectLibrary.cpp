#include "AI/KHUProjectMemory.h"
#include "AI/KHULearningCoach.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/Guid.h"

namespace
{
    TSharedRef<FJsonObject> Copy(const TSharedPtr<FJsonValue>& Value)
    { auto O = MakeShared<FJsonObject>(); O->Values = Value->AsObject()->Values; return O; }
    TSharedRef<FJsonObject> Meta(const FString& Id, const FString& Folder, const FString& Title, bool Archived)
    { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("task"), Id); O->SetStringField(TEXT("folder"), Folder); O->SetStringField(TEXT("title"), Title); O->SetBoolField(TEXT("archived"), Archived); return O; }
}
TSharedRef<FJsonObject> FKHUProjectMemory::LibraryCopy() const
{
    auto Library = MakeShared<FJsonObject>(); const TSharedPtr<FJsonObject>* Existing;
    if (Cache && Cache->TryGetObjectField(TEXT("library"), Existing)) { Library->Values = (*Existing)->Values; }
    else { Library->SetArrayField(TEXT("folders"), {}); Library->SetArrayField(TEXT("conversations"), {}); }
    return Library;
}
bool FKHUProjectMemory::ValidLibrary(const TSharedRef<FJsonObject>& Library, const TSet<FString>& Tasks) const
{
    const TArray<TSharedPtr<FJsonValue>> *Folders, *Conversations;
    if (!Library->TryGetArrayField(TEXT("folders"), Folders) || Folders->Num() > 128
        || !Library->TryGetArrayField(TEXT("conversations"), Conversations) || Conversations->Num() > Policy.ArchiveMaxTurns) { return false; }
    TSet<FString> FolderIds, ConversationIds;
    for (const auto& V : *Folders)
    {
        if (!V || V->Type != EJson::Object) { return false; } const auto O = V->AsObject(); FString Id, Title; bool Archived;
        if (!O->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || Id.Len() > 128 || FolderIds.Contains(Id)
            || !O->TryGetStringField(TEXT("title"), Title) || Title.TrimStartAndEnd().IsEmpty() || Title.Len() > 80 || !O->TryGetBoolField(TEXT("archived"), Archived)) { return false; }
        FolderIds.Add(Id);
    }
    for (const auto& V : *Conversations)
    {
        if (!V || V->Type != EJson::Object) { return false; } const auto O = V->AsObject(); FString Id, Folder, Title; bool Archived;
        if (!O->TryGetStringField(TEXT("task"), Id) || !Tasks.Contains(Id) || ConversationIds.Contains(Id)
            || !O->TryGetStringField(TEXT("folder"), Folder) || (!Folder.IsEmpty() && !FolderIds.Contains(Folder))
            || !O->TryGetStringField(TEXT("title"), Title) || Title.Len() > 80 || !O->TryGetBoolField(TEXT("archived"), Archived)) { return false; }
        ConversationIds.Add(Id);
    }
    return true;
}
bool FKHUProjectMemory::ListConversations(TArray<FKHULearningFolder>& Folders, TArray<FKHUSavedConversation>& Conversations) const
{
    Folders.Reset(); Conversations.Reset(); TArray<TSharedPtr<FJsonValue>> Entries, Notes;
    if (!Read(Entries, Notes)) { return false; } const auto Library = LibraryCopy();
    TMap<FString, TSharedPtr<FJsonObject>> Metadata; TSet<FString> ArchivedFolders;
    for (const auto& V : Library->GetArrayField(TEXT("folders")))
    { const auto O = V->AsObject(); FKHULearningFolder F; F.Id = O->GetStringField(TEXT("id")); F.Title = O->GetStringField(TEXT("title")); F.bArchived = O->GetBoolField(TEXT("archived")); Folders.Add(F); if (F.bArchived) { ArchivedFolders.Add(F.Id); } }
    for (const auto& V : Library->GetArrayField(TEXT("conversations"))) { Metadata.Add(V->AsObject()->GetStringField(TEXT("task")), V->AsObject()); }
    for (int32 I = Entries.Num() - 1; I >= 0; --I)
    {
        const auto E = Entries[I]->AsObject(); FKHUSavedConversation C;
        C.Id = E->GetStringField(TEXT("task")); C.Scope = E->GetStringField(TEXT("scope")); C.Goal = E->GetStringField(TEXT("goal")); C.Updated = E->GetStringField(TEXT("updated_utc"));
        C.Hint = static_cast<int32>(E->GetNumberField(TEXT("hint"))); C.Session = static_cast<int32>(E->GetNumberField(TEXT("session")));
        C.bHasPortfolio = E->HasField(TEXT("portfolio"));
        C.Title = Excerpt(C.Goal, 60).Replace(TEXT("\n"), TEXT(" ")); if (C.Title.IsEmpty()) { C.Title = TEXT("새 대화"); }
        if (const auto* M = Metadata.Find(C.Id))
        { C.Folder = (*M)->GetStringField(TEXT("folder")); C.bArchived = (*M)->GetBoolField(TEXT("archived")); const FString Title = (*M)->GetStringField(TEXT("title")); if (!Title.IsEmpty()) { C.Title = Title; } }
        C.bOwnArchived = C.bArchived; C.bFolderArchived = ArchivedFolders.Contains(C.Folder); C.bArchived |= C.bFolderArchived;
        const TArray<TSharedPtr<FJsonValue>>* Turns = nullptr;
        C.bLegacy = !E->TryGetArrayField(TEXT("turns"), Turns) || (E->HasField(TEXT("legacy_excerpt_only")) && E->GetBoolField(TEXT("legacy_excerpt_only"))) || (Turns && Turns->IsEmpty() && !C.Goal.IsEmpty());
        Conversations.Add(MoveTemp(C));
    }
    return true;
}
bool FKHUProjectMemory::ReadConversation(const FString& Id, int32 Hint, FKHUSavedConversation& Out) const
{
    Out = FKHUSavedConversation(); if (Hint < 0 || Hint > 3) { LastError = TEXT("도움 수준을 확인해 주세요."); return false; }
    TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations;
    if (!ListConversations(Folders, Conversations)) { return false; }
    const auto* Found = Conversations.FindByPredicate([&](const auto& C) { return C.Id == Id; });
    if (!Found) { LastError = TEXT("선택한 대화를 찾지 못했습니다."); return false; } Out = *Found;
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; }
    const auto* Entry = Entries.FindByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("task")) == Id; });
    if (!Entry) { LastError = TEXT("대화 목록이 변경됐습니다. 다시 선택해 주세요."); return false; }
    if (Out.bLegacy)
    { const auto E = (*Entry)->AsObject(); Out.LegacySummary = TEXT("목표: ") + Out.Goal + TEXT("\n학생 기록: ") + E->GetStringField(TEXT("student_report"));
      const int32 AdviceHint = static_cast<int32>(E->HasField(TEXT("ai_suggestion_hint")) ? E->GetNumberField(TEXT("ai_suggestion_hint")) : E->GetNumberField(TEXT("hint")));
      if (Hint >= AdviceHint) { Out.LegacySummary += TEXT("\n이전 AI 제안(미검증): ") + E->GetStringField(TEXT("ai_suggestion_unverified")); } }
    const TSharedPtr<FJsonObject>* Portfolio;
    if ((*Entry)->AsObject()->TryGetObjectField(TEXT("portfolio"), Portfolio))
    {
        Out.bPortfolioHidden = (*Portfolio)->GetNumberField(TEXT("hint")) > Hint;
        if (!Out.bPortfolioHidden)
        { TSharedPtr<FJsonObject> Detached; KHUMcpProtocol::ReadObject(KHUMcpProtocol::WriteObject((*Portfolio).ToSharedRef()), Detached); Out.Portfolio = Detached; }
    }
    const TSharedPtr<FJsonObject>* Coach;
    if ((*Entry)->AsObject()->TryGetObjectField(TEXT("coach"), Coach)) { Out.Coach = *Coach; }
    const TSharedPtr<FJsonObject>* Budget;
    if ((*Entry)->AsObject()->TryGetObjectField(TEXT("runtime_budget"), Budget))
    { Out.bHasBudget = true; Out.Spent = (*Budget)->GetNumberField(TEXT("spent")); Out.Allowance = (*Budget)->GetNumberField(TEXT("allowance")); Out.bCostKnown = (*Budget)->GetBoolField(TEXT("cost_known")); }
    const TArray<TSharedPtr<FJsonValue>>* Turns;
    if ((*Entry)->AsObject()->TryGetArrayField(TEXT("turns"), Turns))
    {
        int32 Students = 0;
        for (const auto& V : *Turns)
        {
            const auto R = V->AsObject(); const FString Role = R->GetStringField(TEXT("role")), TurnId = R->GetStringField(TEXT("id"));
            if (Role == TEXT("user")) { ++Students; if (TurnId.StartsWith(TEXT("u")) && TurnId.Mid(1).IsNumeric()) { Out.NextTurn = FMath::Max(Out.NextTurn, FCString::Atoi(*TurnId.Mid(1)) + 1); } }
            if (Role == TEXT("assistant") && R->GetNumberField(TEXT("hint")) > Hint) { ++Out.HiddenReplies; continue; }
            Out.Turns.Add(V);
        }
        Out.NextTurn = FMath::Max(Out.NextTurn, Students + 1);
    }
    return true;
}
bool FKHUProjectMemory::CreateFolder(const FString& Title, FString& Id)
{
    Id.Reset(); const FString Safe = Excerpt(Title, 80);
    if (Safe.IsEmpty() || Title.Len() > 80 || !Policy.bProjectMemory) { LastError = TEXT("폴더 이름을 1~80자로 입력해 주세요."); return false; }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; } auto Library = LibraryCopy(); auto Folders = Library->GetArrayField(TEXT("folders"));
    if (Folders.Num() >= 128) { LastError = TEXT("학습 폴더는 최대128개까지 만들 수 있어요."); return false; }
    const FString NewId = FGuid::NewGuid().ToString(EGuidFormats::Digits); auto F = MakeShared<FJsonObject>();
    F->SetStringField(TEXT("id"), NewId); F->SetStringField(TEXT("title"), Safe); F->SetBoolField(TEXT("archived"), false);
    Folders.Add(MakeShared<FJsonValueObject>(F)); Library->SetArrayField(TEXT("folders"), Folders);
    if (!Write(Entries, Notes, Library)) { return false; } Id = NewId; return true;
}
bool FKHUProjectMemory::EditFolder(const FString& Id, const FString& Title, bool Archived)
{
    const FString Safe = Excerpt(Title, 80); if (Safe.IsEmpty() || Title.Len() > 80 || !Policy.bProjectMemory) { LastError = TEXT("폴더 이름을 1~80자로 입력해 주세요."); return false; }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; } auto Library = LibraryCopy(); auto Folders = Library->GetArrayField(TEXT("folders"));
    const int32 At = Folders.IndexOfByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("id")) == Id; });
    if (At == INDEX_NONE) { LastError = TEXT("폴더를 찾지 못했어요."); return false; } auto F = Copy(Folders[At]);
    F->SetStringField(TEXT("title"), Safe); F->SetBoolField(TEXT("archived"), Archived); Folders[At] = MakeShared<FJsonValueObject>(F); Library->SetArrayField(TEXT("folders"), Folders); return Write(Entries, Notes, Library);
}
bool FKHUProjectMemory::CreateConversation(const FString& Scope, const FString& Folder, int32 Hint, FString& Id)
{
    Id.Reset(); if (!Policy.bProjectMemory || Scope.Len() > 1200 || Hint < 0 || Hint > 3) { LastError = TEXT("대화의 자산 범위 또는 도움 수준을 확인해 주세요."); return false; }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; } auto Library = LibraryCopy();
    if (!Folder.IsEmpty() && !Library->GetArrayField(TEXT("folders")).ContainsByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("id")) == Folder && !V->AsObject()->GetBoolField(TEXT("archived")); }))
    { LastError = TEXT("새 대화를 만들 폴더를 선택해 주세요. 보관된 폴더는 먼저 복원해야 합니다."); return false; }
    const FString NewId = FGuid::NewGuid().ToString(EGuidFormats::Digits); Entry(NewId, Scope, TEXT(""), Hint, 1, Entries);
    auto Conversations = Library->GetArrayField(TEXT("conversations")); Conversations.Add(MakeShared<FJsonValueObject>(Meta(NewId, Folder, TEXT(""), false))); Library->SetArrayField(TEXT("conversations"), Conversations);
    if (!Write(Entries, Notes, Library)) { return false; } Id = NewId; return true;
}
bool FKHUProjectMemory::EditConversation(const FString& Id, const FString& Title, const FString& Folder, bool Archived)
{
    if (Title.Len() > 80 || !Policy.bProjectMemory) { LastError = TEXT("대화 이름은80자 이내로 입력해 주세요."); return false; }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; } auto Library = LibraryCopy();
    if (!Entries.ContainsByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("task")) == Id; })) { LastError = TEXT("대화를 찾지 못했어요."); return false; }
    if (!Folder.IsEmpty() && !Library->GetArrayField(TEXT("folders")).ContainsByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("id")) == Folder; })) { LastError = TEXT("이동할 폴더를 찾지 못했어요."); return false; }
    auto Conversations = Library->GetArrayField(TEXT("conversations")); Conversations.RemoveAll([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("task")) == Id; });
    Conversations.Add(MakeShared<FJsonValueObject>(Meta(Id, Folder, Excerpt(Title, 80), Archived))); Library->SetArrayField(TEXT("conversations"), Conversations); return Write(Entries, Notes, Library);
}
bool FKHUProjectMemory::SaveConversationBudget(const FString& Id, double Spent, double Allowance, bool Known)
{
    if (!Policy.bProjectMemory || !FMath::IsFinite(Spent) || !FMath::IsFinite(Allowance) || Spent < 0 || Allowance < 0) { LastError = TEXT("대화의 비용 상태를 저장할 수 없습니다."); return false; }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; }
    const int32 At = Entries.IndexOfByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("task")) == Id; });
    if (At == INDEX_NONE) { LastError = TEXT("비용을 저장할 대화를 찾지 못했어요."); return false; }
    auto E = Copy(Entries[At]); auto Budget = MakeShared<FJsonObject>();
    Budget->SetNumberField(TEXT("spent"), Spent); Budget->SetNumberField(TEXT("allowance"), Allowance); Budget->SetBoolField(TEXT("cost_known"), Known);
    E->SetObjectField(TEXT("runtime_budget"), Budget); Entries[At] = MakeShared<FJsonValueObject>(E); return Write(Entries, Notes);
}

bool FKHUProjectMemory::SaveCoach(const FString& Id, const FString& Scope, const TSharedRef<FJsonObject>& State)
{
    if (!Policy.bProjectMemory || !FKHULearningCoach::Valid(State)) { LastError = TEXT("질문 돌아보기 상태를 저장할 수 없습니다."); return false; }
    FKHUSavedConversation Current;
    if (!ReadConversation(Id, 3, Current) || Current.Scope != Scope || Current.bArchived) { LastError = TEXT("대화와 자산 범위를 확인해 주세요."); return false; }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; }
    const int32 At = Entries.IndexOfByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("task")) == Id; });
    if (At == INDEX_NONE) { return false; }
    // Copy/redact quotes rather than sharing mutable UI/model objects with the archive cache.
    TSharedPtr<FJsonObject> Safe; if (!KHUMcpProtocol::ReadObject(KHUMcpProtocol::WriteObject(State), Safe)) { return false; }
    auto Redact = [](const TSharedPtr<FJsonObject>& R)
    { R->SetStringField(TEXT("quote"), Excerpt(R->GetStringField(TEXT("quote")), 240));
      for (const auto& V : R->GetArrayField(TEXT("items"))) { const auto I = V->AsObject(); I->SetStringField(TEXT("quote"), Excerpt(I->GetStringField(TEXT("quote")), 240)); } };
    for (const auto& V : Safe->GetArrayField(TEXT("events"))) { Redact(V->AsObject()); }
    const TSharedPtr<FJsonObject>* R; if (Safe->TryGetObjectField(TEXT("review"), R)) { Redact(*R); }
    auto E = Copy(Entries[At]); E->SetObjectField(TEXT("coach"), Safe); Entries[At] = MakeShared<FJsonValueObject>(E); return Write(Entries, Notes);
}

bool FKHUProjectMemory::SavePortfolio(const FString& Id, const FString& Scope, int32 Hint, FKHUPortfolio& Record)
{
    if (!Policy.bProjectMemory || Record.Body.Len() > 16000 || Record.Evidence.Len() > 6000 || !FKHUPortfolio::Valid(Record.Json()) || Record.Hint != Hint)
    { LastError = TEXT("기록 길이·근거 선택·도움 수준을 확인해 주세요."); return false; }
    FKHUSavedConversation C;
    if (!ReadConversation(Id, Hint, C)) { return false; }
    if (C.Scope != Scope || C.bArchived || C.bPortfolioHidden)
    { LastError = TEXT("현재 자산·도움 수준에서 편집할 수 없는 기록입니다."); return false; }
    const FString Revision = C.Portfolio ? C.Portfolio->GetStringField(TEXT("revision")) : TEXT("");
    if (Revision != Record.Revision)
    { LastError = TEXT("다른 창에서 기록이 바뀌었어요. 현재 글을 보존하고 기록을 다시 열어 주세요."); return false; }
    for (const auto& Source : Record.Sources)
    { if (!C.Turns.ContainsByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("id")) == Source; }))
      { LastError = TEXT("선택한 원문을 현재 도움 수준에서 찾지 못했어요."); return false; } }
    TArray<TSharedPtr<FJsonValue>> Entries, Notes; if (!Read(Entries, Notes)) { return false; }
    const int32 At = Entries.IndexOfByPredicate([&](const auto& V) { return V->AsObject()->GetStringField(TEXT("task")) == Id; });
    if (At == INDEX_NONE) { return false; }
    auto Safe = Record; Safe.Revision = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto Object = Safe.Json(); auto E = Copy(Entries[At]); E->SetObjectField(TEXT("portfolio"), Object); Entries[At] = MakeShared<FJsonValueObject>(E);
    if (!Write(Entries, Notes)) { return false; }
    Record = FKHUPortfolio::Read(Object); return true;
}

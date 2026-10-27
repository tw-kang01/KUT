#include "UI/SKHUTutorPanel.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

#define LOCTEXT_NAMESPACE "KHUTutorProjects"
FString SKHUTutorPanel::AssetScope() const { return TargetBlueprint.IsValid() ? TargetBlueprint->GetPathName() : TEXT(""); }
void SKHUTutorPanel::EditProjectName(const FString& Kind, const FString& Id, const FString& Title)
{
    if (Busy() || bMock || bStopped) { return; }
    FSlateApplication::Get().DismissAllMenus(); ShowPage(1);
    EditingKind = Kind; EditingId = Id; ProjectNameInput->SetText(FText::FromString(Title)); LibraryMessage.Reset();
}
void SKHUTutorPanel::SaveProjectName()
{
    if (Busy() || bMock || !ProjectMemory || !ProjectNameInput || EditingKind.IsEmpty()) { return; }
    const FString Title = ProjectNameInput->GetText().ToString().TrimStartAndEnd(); bool Saved = false;
    if (Title.IsEmpty()) { LibraryMessage = TEXT("이름을 입력해 주세요."); return; }
    TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations;
    if (!ProjectMemory->ListConversations(Folders, Conversations)) { LibraryMessage = ProjectMemory->Error(); return; }
    if (EditingKind == TEXT("new_folder")) { Saved = ProjectMemory->CreateFolder(Title, ActiveFolder); }
    else if (EditingKind == TEXT("folder"))
    { if (const auto* F = Folders.FindByPredicate([this](const auto& V) { return V.Id == EditingId; })) { Saved = ProjectMemory->EditFolder(F->Id, Title, F->bArchived); } }
    else if (const auto* C = Conversations.FindByPredicate([this](const auto& V) { return V.Id == EditingId; }))
    { Saved = ProjectMemory->EditConversation(C->Id, Title, C->Folder, C->bOwnArchived); }
    LibraryMessage = Saved ? TEXT("이름을 저장했어요.") : ProjectMemory->Error();
    if (Saved) { EditingKind.Reset(); EditingId.Reset(); ProjectNameInput->SetText(FText::GetEmpty()); }
    RefreshProjects();
}
bool SKHUTutorPanel::SaveCurrentDraft()
{
    if (!Input || bMock) { return true; }
    const FString Draft = Input->GetText().ToString();
    // The first composer has no id until a question is sent. Give an unsent draft
    // its own conversation before navigating, otherwise Drafts[""] is unreachable.
    if (ActiveConversation.IsEmpty() && !Draft.IsEmpty())
    {
        FString Id;
        if (!ProjectMemory || !ProjectMemory->CreateConversation(AssetScope(), ActiveFolder, Hint, Id))
        { Status = LibraryMessage = ProjectMemory ? ProjectMemory->Error() : TEXT("작성 중인 채팅을 보존할 수 없어요. 입력한 내용은 그대로 두었어요."); return false; }
        ActiveConversation = Id;
        ActiveConversationTitle = TEXT("새 채팅");
    }
    if (!ActiveConversation.IsEmpty()) { Drafts.Add(ActiveConversation, Draft); }
    return true;
}
void SKHUTutorPanel::NewConversation(const FString& Folder)
{
    if (bStopped) { return; }
    if (Busy()) { Status = TEXT("답변이 끝나면 새 채팅을 열 수 있어요. 바로 전환하려면 먼저 응답을 멈춰 주세요."); ShowPage(0); return; }
    if (bMock) { ResetTask(false); ShowPage(0); return; }
    if (!SaveCurrentDraft()) { return; }
    FString Id;
    if (!ProjectMemory || !ProjectMemory->CreateConversation(AssetScope(), Folder, Hint, Id))
    { LibraryMessage = ProjectMemory ? ProjectMemory->Error() : TEXT("프로젝트 기록을 사용할 수 없습니다."); ShowPage(1); return; }
    OpenConversation(Id);
}
void SKHUTutorPanel::OpenConversation(const FString& Id)
{
    if (bStopped || bMock || !ProjectMemory) { return; }
    // Re-selecting the open row must preserve live receipts, links and the loop.
    if (!Id.IsEmpty() && Id == ActiveConversation) { FSlateApplication::Get().DismissAllMenus(); ShowPage(0); return; }
    if (Busy()) { Status = TEXT("답변이 끝나면 다른 채팅을 열 수 있어요. 바로 전환하려면 먼저 응답을 멈춰 주세요."); ShowPage(0); return; }
    FKHUSavedConversation Saved;
    if (!ProjectMemory->ReadConversation(Id, Hint, Saved)) { LibraryMessage = ProjectMemory->Error(); return; }
    Saved.Title = FKHUPortfolio::DisplayTitle(Saved);
    if (!SaveCurrentDraft()) { return; }
    FSlateApplication::Get().DismissAllMenus();
    ResetTask(false); ActiveConversation = Id; ActiveConversationTitle = Saved.Title; ActiveFolder = Saved.Folder; StoredConversation = MoveTemp(Saved);
    bConversationReadOnly = StoredConversation.bArchived || StoredConversation.Scope != AssetScope();
    SessionFolderExpansion.Add(ActiveFolder, true);
    Input->SetText(FText::FromString(Drafts.FindRef(Id))); RenderStoredConversation(FMath::Max(0, StoredConversation.Turns.Num() - 40)); ShowPage(0); RefreshProjects();
}
void SKHUTutorPanel::RenderStoredConversation(int32 Start)
{
    if (!Conversation || ActiveConversation != StoredConversation.Id) { return; }
    ClearConversation(); Links.Reset(); CurrentLinkIds.Reset();
    HistoryStart = FMath::Clamp(Start, 0, FMath::Max(0, StoredConversation.Turns.Num() - 1));
    if (StoredConversation.bLegacy) { AddNotice(ENotice::Info, TEXT("이 대화에는 예전 발췌 기록이 포함돼요. 당시 저장되지 않은 원문은 복구할 수 없어요.\n") + StoredConversation.LegacySummary); }
    if (StoredConversation.HiddenReplies > 0) { AddNotice(ENotice::Info, TEXT("현재 도움 수준보다 자세한 과거 답변은 숨겼어요. 도움 수준을 올리면 다시 보여요.")); }
    for (int32 I = HistoryStart; I < FMath::Min(StoredConversation.Turns.Num(), HistoryStart + 40); ++I)
    {
        const auto T = StoredConversation.Turns[I]->AsObject(); const bool User = T->GetStringField(TEXT("role")) == TEXT("user"); double TurnHint = -1;
        if (User || !T->TryGetNumberField(TEXT("hint"), TurnHint)) { TurnHint = -1; }
        AddMessage(User ? LOCTEXT("Student", "나") : LOCTEXT("Tutor", "KHU Tutor"), T->GetStringField(TEXT("text")), false, FMath::Clamp(static_cast<int32>(TurnHint), -1, 3));
    }
    FKHULearningCoach SavedCoach; if (SavedCoach.Restore(StoredConversation.Coach)) { ShownReview = SavedCoach.Id(); AddCoachCard(SavedCoach); }
    Status = StoredConversation.bArchived ? TEXT("보관한 채팅입니다. 채팅 메뉴에서 복원하면 이어갈 수 있어요.")
        : StoredConversation.Scope != AssetScope() ? TEXT("다른 Blueprint의 대화입니다. 해당 Blueprint의 Window → KHU Tutor에서 이어가세요.")
        : TEXT("");
}
TSharedRef<SWidget> SKHUTutorPanel::ProjectMenu(const FString& Id, bool IsFolder)
{
    auto Menu = SNew(SVerticalBox); TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations;
    if (!ProjectMemory || !ProjectMemory->ListConversations(Folders, Conversations)) { return Menu; }
    const auto CanEdit = [this] { return !Busy() && !bMock; };
    if (IsFolder)
    {
        const auto* Found = Folders.FindByPredicate([&](const auto& F) { return F.Id == Id; }); if (!Found) { return Menu; } const auto F = *Found;
        if (!F.bArchived) { Menu->AddSlot().AutoHeight()[SettingsButton(LOCTEXT("NewInFolder", "새 채팅"), [this, F] { NewConversation(F.Id); }, TEXT("Settings.SecondaryButton"), CanEdit)]; }
        Menu->AddSlot().AutoHeight()[SettingsButton(LOCTEXT("RenameFolder", "폴더 이름 변경"), [this, F] { EditProjectName(TEXT("folder"), F.Id, F.Title); }, TEXT("Settings.SecondaryButton"), CanEdit)];
        Menu->AddSlot().AutoHeight()[SettingsButton(F.bArchived ? LOCTEXT("RestoreFolder", "폴더 복원") : LOCTEXT("ArchiveFolder", "폴더 보관"), [this, F]
        { LibraryMessage = ProjectMemory->EditFolder(F.Id, F.Title, !F.bArchived) ? TEXT("폴더 상태를 변경했어요. 대화 원문은 보존됩니다.") : ProjectMemory->Error(); RefreshProjects(); }, TEXT("Settings.SecondaryButton"), CanEdit)];
    }
    else
    {
        const auto* Found = Conversations.FindByPredicate([&](const auto& C) { return C.Id == Id; }); if (!Found) { return Menu; } const auto C = *Found;
        Menu->AddSlot().AutoHeight()[SettingsButton(LOCTEXT("Portfolio", "학습 기록 작성"), [this, C] { FSlateApplication::Get().DismissAllMenus(); OpenPortfolio(C.Id); }, TEXT("Settings.SecondaryButton"),
            [this, C] { return !Busy() && !bMock && !bStopped && !C.bArchived && C.Scope == AssetScope(); })];
        Menu->AddSlot().AutoHeight()[SettingsButton(LOCTEXT("RenameChat", "채팅 이름 변경"), [this, C] { EditProjectName(TEXT("chat"), C.Id, C.Title); }, TEXT("Settings.SecondaryButton"), CanEdit)];
        if (!C.bFolderArchived) { Menu->AddSlot().AutoHeight()[SettingsButton(C.bOwnArchived ? LOCTEXT("RestoreChat", "채팅 복원") : LOCTEXT("ArchiveChat", "채팅 보관"), [this, C]
        { LibraryMessage = ProjectMemory->EditConversation(C.Id, C.Title, C.Folder, !C.bOwnArchived) ? TEXT("대화 상태를 변경했어요. 원문은 보존됩니다.") : ProjectMemory->Error(); RefreshProjects(); }, TEXT("Settings.SecondaryButton"), CanEdit)]; }
        Menu->AddSlot().AutoHeight().Padding(8, 12)[SNew(STextBlock).Text(LOCTEXT("MoveTo", "폴더로 이동"))];
        Menu->AddSlot().AutoHeight()[SettingsButton(LOCTEXT("MoveDefault", "폴더 없이 보기"), [this, C]
        { LibraryMessage = ProjectMemory->EditConversation(C.Id, C.Title, TEXT(""), C.bOwnArchived) ? TEXT("대화를 이동했어요.") : ProjectMemory->Error(); RefreshProjects(); }, TEXT("Settings.SecondaryButton"), CanEdit)];
        for (const auto& F : Folders) { if (!F.bArchived && F.Id != C.Folder)
        { Menu->AddSlot().AutoHeight()[SettingsButton(FText::FromString(F.Title), [this, C, F]
            { LibraryMessage = ProjectMemory->EditConversation(C.Id, C.Title, F.Id, C.bOwnArchived) ? TEXT("대화를 이동했어요.") : ProjectMemory->Error(); RefreshProjects(); }, TEXT("Settings.SecondaryButton"), CanEdit)]; } }
    }
    return SNew(SBox).MaxDesiredWidth(300)[Menu];
}
void SKHUTutorPanel::RefreshProjects()
{
    if ((!ProjectList && !SessionList) || !ProjectMemory || bStopped) { return; }
    if (ProjectList) { ProjectList->ClearChildren(); }
    if (SessionList) { SessionList->ClearChildren(); }
    TArray<FKHULearningFolder> Folders; TArray<FKHUSavedConversation> Conversations;
    if (!ProjectMemory->ListConversations(Folders, Conversations)) { LibraryMessage = ProjectMemory->Error(); return; }
    if (const auto* Active = Conversations.FindByPredicate([this](const auto& C) { return C.Id == ActiveConversation; }))
    { ActiveConversationTitle = Active->Title; bConversationReadOnly = Active->bArchived || Active->Scope != AssetScope(); if (bConversationReadOnly && Loop) { CancelTask(); }
      if (bConversationReadOnly) { Status = Active->bArchived ? TEXT("보관한 채팅입니다. 채팅 메뉴에서 복원하면 이어갈 수 있어요.") : TEXT("다른 Blueprint의 대화입니다. 해당 Blueprint의 Window → KHU Tutor에서 이어가세요."); } }
    FKHULearningFolder Default; Default.Title = TEXT("채팅"); Folders.Insert(Default, 0);
    const auto Populate = [this, &Folders, &Conversations](const TSharedPtr<SVerticalBox>& List, bool bCompact)
    {
    if (!List) { return; }
    for (const auto& Folder : Folders)
    {
        if (Folder.bArchived && !bShowArchived) { continue; } TArray<FKHUSavedConversation> Matching;
        for (const auto& C : Conversations)
        { if (C.Folder == Folder.Id && (bShowArchived || !C.bArchived) && (ProjectQuery.IsEmpty() || C.Title.Contains(ProjectQuery) || Folder.Title.Contains(ProjectQuery))) { Matching.Add(C); } }
        if (!ProjectQuery.IsEmpty() && Matching.IsEmpty() && !Folder.Title.Contains(ProjectQuery)) { continue; }
        auto Rows = SNew(SVerticalBox); const int32 Limit = VisibleCounts.Contains(Folder.Id) ? VisibleCounts[Folder.Id] : 12;
        for (int32 I = 0; I < FMath::Min(Limit, Matching.Num()); ++I)
        {
            const auto C = Matching[I]; FString ScopeName = C.Scope; C.Scope.Split(TEXT("."), nullptr, &ScopeName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
            const bool bHasDraft = !(C.Id == ActiveConversation && Input ? Input->GetText().ToString() : Drafts.FindRef(C.Id)).IsEmpty();
            const FString Title = C.Title == TEXT("새 대화") ? TEXT("새 채팅") : C.Title;
            Rows->AddSlot().AutoHeight().Padding(bCompact ? 0 : 18, 3, 0, 3)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(ActiveConversation == C.Id ? TEXT("Tutor.ListButtonActive") : TEXT("Tutor.ListButton")))
                    .ContentPadding(FMargin(8, 8)).ToolTipText(FText::FromString(Title)).IsEnabled_Lambda([this] { return !bMock; }).OnClicked_Lambda([this, C] { OpenConversation(C.Id); return FReply::Handled(); })
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Title)).AutoWrapText(!bCompact).OverflowPolicy(bCompact ? ETextOverflowPolicy::Ellipsis : ETextOverflowPolicy::Clip)
                            .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.ButtonText")))]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[SNew(STextBlock).Text(FText::FromString((C.Scope.IsEmpty() ? FString(TEXT("일반 질문")) : TEXT("Blueprint · ") + ScopeName)
                            + (ActiveConversation == C.Id ? TEXT(" · 현재 채팅") : TEXT("")) + (bHasDraft ? TEXT(" · 작성 중") : TEXT("")) + (C.Scope != AssetScope() ? TEXT(" · 읽기 전용") : TEXT("")) + (C.bArchived ? TEXT(" · 보관됨") : TEXT(""))))
                            .AutoWrapText(!bCompact).OverflowPolicy(bCompact ? ETextOverflowPolicy::Ellipsis : ETextOverflowPolicy::Clip).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]]]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[SNew(SComboButton).IsEnabled_Lambda([this] { return !Busy() && !bMock; })
                    .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("···")))]
                    .OnGetMenuContent_Lambda([this, C] { return ProjectMenu(C.Id, false); })]];
        }
        if (Matching.Num() > Limit) { Rows->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(18, 8)[Button(LOCTEXT("MoreChats", "더 보기"), [this, Folder, Limit] { VisibleCounts.Add(Folder.Id, Limit + 12); RefreshProjects(); })]; }
        if (Matching.IsEmpty()) { Rows->AddSlot().AutoHeight().Padding(bCompact ? 4 : 18, 8)[SNew(STextBlock).Text(LOCTEXT("EmptyFolder", "새 채팅으로 첫 질문을 시작하세요.")).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]; }
        auto Header = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("ContentBrowser.AssetTreeFolderClosed")))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0)[SNew(SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("SimpleButton"))).ContentPadding(FMargin(0))
                .OnClicked_Lambda([this, Folder] { ActiveFolder = Folder.Id; SessionFolderExpansion.Add(Folder.Id, true); RefreshProjects(); return FReply::Handled(); })
                [SNew(STextBlock).Text(FText::FromString(Folder.Title + (Folder.bArchived ? TEXT(" · 보관됨") : TEXT("")))).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Label")))]];
        Header->AddSlot().AutoWidth()[SNew(SComboButton).IsEnabled_Lambda([this] { return !Busy() && !bMock; })
            .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("···")))]
            .OnGetMenuContent_Lambda([this, Folder]() -> TSharedRef<SWidget>
            { if (!Folder.Id.IsEmpty()) { return ProjectMenu(Folder.Id, true); }
              return SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SettingsButton(LOCTEXT("NewDefaultChat", "새 채팅"), [this] { NewConversation(TEXT("")); }, TEXT("Settings.SecondaryButton"))]; })];
        const bool bCollapsed = ProjectQuery.IsEmpty() && (bCompact ? SessionFolderExpansion.Contains(Folder.Id) && !SessionFolderExpansion.FindRef(Folder.Id) : Folder.Id != ActiveFolder);
        List->AddSlot().AutoHeight().Padding(0, 0, 0, bCompact ? 6 : 12)[SNew(SBorder).BorderImage(Style->GetBrush(bCompact ? TEXT("Tutor.CoachBody") : TEXT("Settings.Card"))).Padding(bCompact ? 2 : 12)
            [SNew(SExpandableArea).InitiallyCollapsed(bCollapsed)
                .OnAreaExpansionChanged_Lambda([this, Folder, bCompact](bool bExpanded) { if (bCompact && ProjectQuery.IsEmpty()) { SessionFolderExpansion.Add(Folder.Id, bExpanded); } })
                .HeaderContent()[Header].BodyContent()[Rows]]];
    }
    if (List->GetChildren()->Num() == 0)
    { List->AddSlot().AutoHeight().Padding(4, 12)[SNew(STextBlock).Text(LOCTEXT("NoMatchingChats", "일치하는 채팅이 없어요. 검색어를 바꿔 주세요.")).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]; }
    };
    Populate(ProjectList, false); Populate(SessionList, true);
}
TSharedRef<SWidget> SKHUTutorPanel::BuildSessionSidebar()
{
    return SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Header"))).Padding(FMargin(10, 14, 10, 10))
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 3)[SNew(STextBlock).Text(LOCTEXT("ProjectChats", "프로젝트 채팅")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Label")))]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 12)[SNew(STextBlock).Text(FText::FromString(FApp::GetProjectName())).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                .ToolTipText(FText::FromString(FApp::GetProjectName())).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[StyledButton(LOCTEXT("SidebarNewChat", "새 채팅"), TEXT("Tutor.PrimaryButton"), [this] { NewConversation(ActiveFolder); }, [this] { return !bStopped; }, FAppStyle::GetBrush(TEXT("Icons.Plus")), LOCTEXT("SidebarNewChatTip", "현재 프로젝트에서 별도의 채팅을 시작해요. 작성 중인 질문은 원래 채팅에 남아요."))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[SNew(SEditableTextBox).Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
                .HintText(LOCTEXT("SidebarSearch", "채팅 검색")).Text_Lambda([this] { return FText::FromString(ProjectQuery); })
                .OnTextChanged_Lambda([this](const FText& Text) { ProjectQuery = Text.ToString(); RefreshProjects(); })]
            + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(SessionList, SVerticalBox)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[StyledButton(LOCTEXT("SidebarManageChats", "채팅·폴더 관리"), TEXT("Tutor.GhostButton"), [this] { ShowPage(1); }, {}, nullptr, LOCTEXT("SidebarManageTip", "폴더 만들기, 이름 변경, 보관한 채팅과 중요 메모를 관리해요."))]];
}
TSharedRef<SWidget> SKHUTutorPanel::BuildProjectsPage()
{
    auto Actions = SNew(SWrapBox).UseAllottedSize(true);
    Actions->AddSlot().Padding(0, 0, 8, 8)[SettingsButton(LOCTEXT("NewChat", "새 채팅"), [this] { NewConversation(ActiveFolder); }, TEXT("Settings.PrimaryButton"), [this] { return !bStopped; })];
    Actions->AddSlot().Padding(0, 0, 8, 8)[SettingsButton(LOCTEXT("NewFolder", "폴더 만들기"), [this] { EditProjectName(TEXT("new_folder"), TEXT(""), TEXT("")); }, TEXT("Settings.SecondaryButton"), [this] { return !Busy() && !bMock; })];
    Actions->AddSlot().Padding(0, 0, 0, 8)[SettingsButton(LOCTEXT("Refresh", "새로고침"), [this] { RefreshProjects(); }, TEXT("Settings.SecondaryButton"))];
    auto NoteKinds = SNew(SWrapBox).UseAllottedSize(true); const FString Kinds[] = {TEXT("goal"), TEXT("decision"), TEXT("completed"), TEXT("attempt"), TEXT("open"), TEXT("next")};
    const FText Labels[] = {LOCTEXT("Goal", "목표"), LOCTEXT("Decision", "결정"), LOCTEXT("Completed", "완료"), LOCTEXT("Attempt", "시도"), LOCTEXT("Open", "남은 문제"), LOCTEXT("Next", "다음 단계")};
    for (int32 I = 0; I < 6; ++I) { const FString Kind = Kinds[I]; NoteKinds->AddSlot().Padding(2)[SNew(SCheckBox).Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox")))
        .IsChecked_Lambda([this, Kind] { return MemoryKind == Kind ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this, Kind](ECheckBoxState) { MemoryKind = Kind; })[SNew(STextBlock).Text(Labels[I])]]; }
    return SNew(SScrollBox) + SScrollBox::Slot().Padding(14, 14)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(STextBlock).Text(FText::FromString(FString(FApp::GetProjectName()) + TEXT(" · 채팅"))).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Section")))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[SNew(STextBlock).Text(LOCTEXT("ProjectsIntro", "질문마다 새 채팅을 만들고, 목록에서 선택해 이어 가세요. 폴더로 과제별 채팅을 묶을 수 있어요.")).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Body")))]
        + SVerticalBox::Slot().AutoHeight()[Actions]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 12)[SNew(SEditableTextBox).Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
            .HintText(LOCTEXT("Search", "채팅·폴더 검색")).Text_Lambda([this] { return FText::FromString(ProjectQuery); }).OnTextChanged_Lambda([this](const FText& Text) { ProjectQuery = Text.ToString(); RefreshProjects(); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[SNew(SCheckBox).IsChecked_Lambda([this] { return bShowArchived ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bShowArchived = State == ECheckBoxState::Checked; RefreshProjects(); })[SNew(STextBlock).Text(LOCTEXT("ShowArchived", "보관한 채팅·폴더 보기")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Body")))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[SNew(SBox).Visibility_Lambda([this] { return EditingKind.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Settings.Card"))).Padding(14)[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock).Text_Lambda([this] { return EditingKind == TEXT("new_folder") ? LOCTEXT("CreateFolderTitle", "새 학습 폴더") : LOCTEXT("RenameTitle", "이름 변경"); }).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Label")))]
                + SVerticalBox::Slot().AutoHeight()[SAssignNew(ProjectNameInput, SEditableTextBox).Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
                    .HintText(LOCTEXT("NameHint", "예: 문 만들기 · 최대80자")).OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Type) { if (Type == ETextCommit::OnEnter) { SaveProjectName(); } })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[SettingsButton(LOCTEXT("SaveName", "저장"), [this] { SaveProjectName(); }, TEXT("Settings.PrimaryButton"), [this] { return !Busy() && ProjectNameInput && !ProjectNameInput->GetText().IsEmpty(); })]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SettingsButton(LOCTEXT("CancelName", "취소"), [this] { EditingKind.Reset(); EditingId.Reset(); }, TEXT("Settings.SecondaryButton"))]]]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Notice.Info"))).Padding(12)
            .Visibility_Lambda([this] { return LibraryMessage.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(LibraryMessage); }).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Body")))]]
        + SVerticalBox::Slot().AutoHeight()[SAssignNew(ProjectList, SVerticalBox)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12)[SNew(SExpandableArea).InitiallyCollapsed(true)
            .HeaderContent()[SNew(STextBlock).Text(LOCTEXT("ImportantNotes", "중요 메모")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Label")))]
            .BodyContent()[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12)[SNew(STextBlock).Text(LOCTEXT("SharedNotes", "메모는 현재 Blueprint와 프로젝트 공통 진행을 이어갈 때 참고해요.")).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))]
                + SVerticalBox::Slot().AutoHeight()[NoteKinds]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(SBox).HeightOverride(72)[SAssignNew(MemoryInput, SMultiLineEditableTextBox).HintText(LOCTEXT("NoteHint", "계속 기억할 결정이나 다음 단계를 적어 주세요."))]]
                + SVerticalBox::Slot().AutoHeight()[SettingsButton(LOCTEXT("SaveNote", "중요 메모 저장"), [this] { RememberProgress(); }, TEXT("Settings.SecondaryButton"), [this] { return !Busy() && !bMock && MemoryInput && !MemoryInput->GetText().IsEmpty(); })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12)[SNew(STextBlock).Text(this, &SKHUTutorPanel::ProjectProgressLabel).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))]]]];
}
#undef LOCTEXT_NAMESPACE

#include "UI/SKHUTutorPanel.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

#define LOCTEXT_NAMESPACE "KHUTutorPortfolio"
bool SKHUTutorPanel::PortfolioAllowed() const
{
    return !bStopped && !bMock && !Busy() && ProjectMemory && !PortfolioConversation.Id.IsEmpty()
        && PortfolioConversation.Scope == AssetScope() && PortfolioRecord.Hint == Hint && !PortfolioConversation.bArchived;
}
void SKHUTutorPanel::OpenPortfolio(const FString& Id)
{
    if (bStopped || bMock || Busy() || !ProjectMemory) { return; }
    if (bPortfolioDirty)
    { if (PortfolioConversation.Id != Id) { PortfolioMessage = TEXT("저장하지 않은 기록이 있어요. ‘기록 저장’ 후 다른 기록을 열어 주세요."); } ShowPage(3); return; }
    FKHUSavedConversation C;
    if (!ProjectMemory->ReadConversation(Id, Hint, C)) { const FString Error = ProjectMemory->Error(); OpenRecords(); RecordsMessage = Error; return; }
    if (C.Scope != AssetScope() || C.bArchived || C.bPortfolioHidden)
    { OpenRecords(); RecordsMessage = TEXT("이 기록의 도움 수준과 연결된 Blueprint를 확인하거나 보관을 해제해 주세요. 기존 기록은 보존됩니다."); return; }
    C.Title = FKHUPortfolio::DisplayTitle(C);
    PortfolioConversation = C; PortfolioRecord = C.Portfolio ? FKHUPortfolio::Read(C.Portfolio) : FKHUPortfolio(); PortfolioRecord.Hint = Hint;
    PortfolioSelection = PortfolioRecord.Sources; PortfolioSourceStart = 0;
    PortfolioBody->SetText(FText::FromString(PortfolioRecord.Body)); PortfolioEvidence->SetText(FText::FromString(PortfolioRecord.Evidence));
    // SetText may invoke editing callbacks; restore the persisted confirmation afterwards.
    if (C.Portfolio) { PortfolioRecord = FKHUPortfolio::Read(C.Portfolio); }
    PortfolioRecord.Hint = Hint;
    PortfolioStep = PortfolioRecord.Body.IsEmpty() ? 0 : 1;
    bPortfolioDirty = false; PortfolioMessage = PortfolioRecord.Body.IsEmpty()
        ? TEXT("남기고 싶은 질문과 답변을 고르세요. 선택한 대화를 보면서 내가 한 일과 배운 점을 기록할 수 있어요.")
        : TEXT("저장한 기록을 열었어요. 내용을 수정한 뒤 저장하거나 ‘3. 저장·공유’로 이동하세요.");
    RefreshPortfolioSources(); ShowPage(3);
}
void SKHUTutorPanel::RefreshPortfolioSources()
{
    PortfolioSources->ClearChildren();
    for (int32 I = PortfolioSourceStart; I < FMath::Min(PortfolioSourceStart + 20, PortfolioConversation.Turns.Num()); ++I)
    {
        const auto T = PortfolioConversation.Turns[I]->AsObject(); const FString Id = T->GetStringField(TEXT("id"));
        const FString Role = T->GetStringField(TEXT("role")) == TEXT("user") ? TEXT("내 질문") : TEXT("AI 답변 · 사실 확인 필요");
        const FString Original = T->GetStringField(TEXT("role")) == TEXT("user") ? FKHUPortfolio::DisplayQuestion(T->GetStringField(TEXT("text"))) : T->GetStringField(TEXT("text"));
        FString Preview = FKHUProjectMemory::Excerpt(Original, 140); Preview.ReplaceInline(TEXT("\n"), TEXT(" ")); Preview.ReplaceInline(TEXT("\r"), TEXT(" "));
        PortfolioSources->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Inset"))).Padding(12)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox)
            .IsChecked_Lambda([this, Id] { return PortfolioSelection.Contains(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Id](ECheckBoxState State)
            {
                if (!PortfolioAllowed()) { return; }
                if (State == ECheckBoxState::Checked)
                { if (PortfolioSelection.Num() >= 20) { PortfolioMessage = TEXT("대화는 최대 20개까지 선택할 수 있어요. 필요 없는 선택을 해제한 뒤 다시 골라 주세요."); return; } PortfolioSelection.AddUnique(Id); }
                else { PortfolioSelection.Remove(Id); }
                PortfolioRecord.Sources = PortfolioSelection; PortfolioRecord.bConfirmed = false; bPortfolioDirty = true;
            })[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong"))).AutoWrapText(true)
                .Text(FText::FromString(FString::Printf(TEXT("%d. %s"), I + 1, *Role)))]]
            + SVerticalBox::Slot().AutoHeight().Padding(22, 6, 0, 6)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))
                .AutoWrapText(true).Text(FText::FromString(Preview))]
            + SVerticalBox::Slot().AutoHeight().Padding(18, 0, 0, 0)[SNew(SExpandableArea).InitiallyCollapsed(true)
                .BorderImage(Style->GetBrush(TEXT("Tutor.CoachBody"))).BodyBorderImage(Style->GetBrush(TEXT("Tutor.CoachBody")))
                .HeaderContent()[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.AccentLabel")))
                    .Text(LOCTEXT("ReadSource", "전체 메시지 보기"))]
                .BodyContent()[SNew(SBox).HeightOverride(180)
                    [SNew(SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true).Text(FText::FromString(Original))
                        .Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Tutor.InputText")))
                        .Font(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).Font)
                        .ReadOnlyForegroundColor(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).ColorAndOpacity)]]]]];
    }
}
void SKHUTutorPanel::GeneratePortfolio()
{
    if (!PortfolioAllowed()) { return; }
    if (!PortfolioBody->GetText().IsEmpty()) { PortfolioStep = 1; PortfolioMessage = TEXT("작성한 기록을 보존했어요. 아래에서 이어서 수정하세요. 선택한 대화는 ‘1. 참고할 대화’에서 다시 볼 수 있어요."); return; }
    if (PortfolioSelection.IsEmpty()) { PortfolioStep = 0; PortfolioMessage = TEXT("기록에 참고할 질문이나 답변을 하나 이상 선택해 주세요."); return; }
    const FString Revision = PortfolioRecord.Revision;
    PortfolioRecord = FKHUPortfolio::Draft(PortfolioConversation, PortfolioSelection, Hint); PortfolioRecord.Revision = Revision;
    PortfolioBody->SetText(FText::FromString(PortfolioRecord.Body));
    if (PortfolioEvidence->GetText().IsEmpty()) { PortfolioEvidence->SetText(FText::FromString(PortfolioRecord.Evidence)); }
    PortfolioStep = 1; bPortfolioDirty = true;
    PortfolioMessage = TEXT("작성할 항목을 준비했어요. 안내 문장을 내 설명으로 바꿔 주세요. 선택한 대화는 별도로 보관되며, 해결 여부는 직접 확인해야 해요.");
}
bool SKHUTutorPanel::SavePortfolio()
{
    if (!PortfolioAllowed()) { PortfolioMessage = TEXT("기록의 자산·도움 수준을 다시 확인해 주세요."); return false; }
    if (PortfolioSelection.IsEmpty()) { PortfolioStep = 0; PortfolioMessage = TEXT("저장하기 전에 참고한 대화를 하나 이상 선택해 주세요."); return false; }
    PortfolioRecord.Body = PortfolioBody->GetText().ToString(); PortfolioRecord.Evidence = PortfolioEvidence->GetText().ToString(); PortfolioRecord.Sources = PortfolioSelection;
    if (!ProjectMemory->SavePortfolio(PortfolioConversation.Id, AssetScope(), Hint, PortfolioRecord))
    { PortfolioMessage = ProjectMemory->Error(); return false; }
    const bool Confirmed = PortfolioRecord.bConfirmed;
    PortfolioBody->SetText(FText::FromString(PortfolioRecord.Body)); PortfolioEvidence->SetText(FText::FromString(PortfolioRecord.Evidence)); PortfolioRecord.bConfirmed = Confirmed;
    bPortfolioDirty = false; PortfolioMessage = TEXT("이 대화에 저장했어요. 원문과 시도 기록은 그대로 남습니다."); return true;
}
bool SKHUTutorPanel::PortfolioExport(FString& Text)
{
    Text.Reset();
    if (!PortfolioAllowed() || !PortfolioRecord.bConfirmed || !SavePortfolio()) { return false; }
    Text = PortfolioRecord.Body + TEXT("\n\n확인 방법과 결과\n") + PortfolioRecord.Evidence; return true;
}
void SKHUTutorPanel::PreparePortfolioPrompt()
{
    if (!PortfolioAllowed()) { return; }
    const bool Refreshing = RecordDrafts.Contains(PortfolioConversation.Id);
    if ((!Input->GetText().IsEmpty() && !(Refreshing && PortfolioConversation.Id == ActiveConversation))
        || (PortfolioConversation.Id != ActiveConversation && !Drafts.FindRef(PortfolioConversation.Id).IsEmpty() && !Refreshing))
    { bDraftConflict = true; PortfolioMessage = TEXT("작성 중인 질문을 보존했어요. ‘대화로 돌아가기’에서 먼저 보내거나 비운 뒤 ‘설명 다듬기’를 눌러 주세요."); Status = PortfolioMessage; return; }
    if (!SavePortfolio()) { return; }
    const FString Prompt = PortfolioRecord.Prompt(PortfolioConversation);
    if (Prompt.Len() > 32000) { PortfolioMessage = TEXT("참고 자료가 너무 많아요. 선택한 대화나 작성한 내용을 줄인 뒤 다시 눌러 주세요."); return; }
    OpenConversation(PortfolioConversation.Id);
    if (ActiveConversation != PortfolioConversation.Id || bConversationReadOnly) { return; }
    const FString Ready = TEXT("질문과 참고 자료를 준비했어요. 내용을 확인하고 보내세요. 받은 설명은 학습 기록에 옮겨 직접 확인한 뒤 저장하세요.");
    if (!Input->GetText().IsEmpty() && Refreshing) { Status = Ready; bDraftConflict = false; }
    else if (!InsertDraft(FText::FromString(FKHUPortfolio::Question()), Ready)) { return; }
    FRecordDraft Draft; Draft.Context = Prompt; Draft.Title = PortfolioConversation.Title; Draft.Revision = PortfolioRecord.Revision;
    Draft.Hint = Hint; Draft.Count = PortfolioSelection.Num(); RecordDrafts.Add(ActiveConversation, MoveTemp(Draft));
}
TSharedRef<SWidget> SKHUTutorPanel::BuildPortfolioPage()
{
    auto Label = [this](const TCHAR* Text, const TCHAR* Name = TEXT("Tutor.Body"))
    { return SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(Name)).Text(FText::FromString(Text)).AutoWrapText(true); };
    auto Edit = [this](const FText&) { PortfolioRecord.bConfirmed = false; bPortfolioDirty = true; };
    auto Step = [this, Label](int32 Index, const TCHAR* Title)
    {
        return SNew(SCheckBox).Style(&Style->GetWidgetStyle<FCheckBoxStyle>(TEXT("Settings.Choice")))
            .IsChecked_Lambda([this, Index] { return PortfolioStep == Index ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState)
            {
                if (!PortfolioAllowed()) { return; }
                if (Index > 0 && PortfolioSelection.IsEmpty()) { PortfolioStep = 0; PortfolioMessage = TEXT("먼저 기록에 참고할 대화를 하나 이상 선택해 주세요."); return; }
                if (Index == 2 && (PortfolioBody->GetText().IsEmpty() || PortfolioEvidence->GetText().IsEmpty()))
                { PortfolioStep = 1; PortfolioMessage = TEXT("본문과 확인 방법·결과를 작성한 뒤 복사할 수 있어요."); return; }
                PortfolioStep = Index;
            })[Label(Title, TEXT("Tutor.Strong"))];
    };
    auto CanConfirm = [this]
    { return PortfolioAllowed() && !PortfolioSelection.IsEmpty() && !PortfolioBody->GetText().ToString().TrimStartAndEnd().IsEmpty() && !PortfolioEvidence->GetText().ToString().TrimStartAndEnd().IsEmpty(); };
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(16, 12, 16, 8)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Label(TEXT("문제 해결 기록"), TEXT("Settings.Section"))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))
                .AutoWrapText(true).Text_Lambda([this] { return FText::FromString(PortfolioConversation.Title); })]
            + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6))
                + SWrapBox::Slot()[Step(0, TEXT("1. 참고할 대화"))]
                + SWrapBox::Slot()[Step(1, TEXT("2. 내 설명"))]
                + SWrapBox::Slot()[Step(2, TEXT("3. 저장·공유"))]]]
        + SVerticalBox::Slot().AutoHeight().Padding(16, 0, 16, 8)
        [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Notice.Info"))).Padding(12)
            [SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong"))).AutoWrapText(true)
                .Text_Lambda([this] { return FText::FromString(PortfolioMessage); })]]
        + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)
            + SScrollBox::Slot().Padding(16, 0, 16, 12)
            [SNew(SVerticalBox).Visibility_Lambda([this] { return PortfolioAllowed() ? EVisibility::Visible : EVisibility::Collapsed; })
                + SVerticalBox::Slot().AutoHeight()[SNew(SVerticalBox).Visibility_Lambda([this] { return PortfolioStep == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))
                        .Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("기록에 참고할 대화 · %d/20개 선택"), PortfolioSelection.Num())); })]
                    + SVerticalBox::Slot().AutoHeight()[SAssignNew(PortfolioSources, SVerticalBox)]
                    + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6))
                        + SWrapBox::Slot()[Button(LOCTEXT("Previous", "이전 대화"), [this] { PortfolioSourceStart = FMath::Max(0, PortfolioSourceStart - 20); RefreshPortfolioSources(); }, [this] { return PortfolioSourceStart > 0; })]
                        + SWrapBox::Slot()[Button(LOCTEXT("Next", "다음 대화"), [this] { PortfolioSourceStart += 20; RefreshPortfolioSources(); }, [this] { return PortfolioSourceStart + 20 < PortfolioConversation.Turns.Num(); })]]]
                + SVerticalBox::Slot().AutoHeight()[SNew(SVerticalBox).Visibility_Lambda([this] { return PortfolioStep == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                    + SVerticalBox::Slot().AutoHeight()[Label(TEXT("내 문제 해결 과정"), TEXT("Tutor.Strong"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[Label(TEXT("선택한 대화를 참고해 직접 한 일, 판단한 이유, 확인한 결과를 내 말로 남기세요. AI 답변을 붙여 넣었다면 사실을 확인하세요."))]
                    + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(280)
                        [SAssignNew(PortfolioBody, SMultiLineEditableTextBox).AutoWrapText(true).OnTextChanged_Lambda(Edit)
                            .Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
                            .Font(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).Font)
                            .HintText(LOCTEXT("BodyHint", "문제, 시도, 판단한 이유, 확인한 결과를 적으세요."))]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 12)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))
                        .Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("%d / 16,000자"), PortfolioBody->GetText().ToString().Len())); })]
                    + SVerticalBox::Slot().AutoHeight()[Label(TEXT("확인 방법과 결과"), TEXT("Tutor.Strong"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[Label(TEXT("공유해도 되는 코드, 테스트 조건, 관찰 결과를 적으세요. 아직 확인하지 않았다면 ‘미확인’으로 남겨도 됩니다."))]
                    + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(120)
                        [SAssignNew(PortfolioEvidence, SMultiLineEditableTextBox).AutoWrapText(true).OnTextChanged_Lambda(Edit)
                            .Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
                            .Font(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).Font)
                            .HintText(LOCTEXT("EvidenceHint", "테스트한 조건, 기대했던 동작, 실제 관찰한 결과를 적으세요."))]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 12)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))
                        .Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("%d / 6,000자"), PortfolioEvidence->GetText().ToString().Len())); })]
                    + SVerticalBox::Slot().AutoHeight()[StyledButton(LOCTEXT("AI", "설명 다듬기"), TEXT("Tutor.SecondaryButton"), [this] { PreparePortfolioPrompt(); })]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Label(TEXT("대화에서 발췌한 내용과 내 글로, 다른 사람이 이해하기 쉬운 설명을 요청해요. 꼭 담을 내용은 위에 적고 채팅에서 질문을 확인해 보내세요."))]]
                + SVerticalBox::Slot().AutoHeight()[SNew(SVerticalBox).Visibility_Lambda([this] { return PortfolioStep == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                    + SVerticalBox::Slot().AutoHeight()[Label(TEXT("공유할 기록 미리보기"), TEXT("Tutor.Strong"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(SBox).HeightOverride(240)
                        [SNew(SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true)
                            .Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
                            .Font(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).Font)
                            .ReadOnlyForegroundColor(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).ColorAndOpacity)
                            .Text_Lambda([this] { return FText::FromString(PortfolioBody->GetText().ToString() + TEXT("\n\n확인 방법과 결과\n") + PortfolioEvidence->GetText().ToString()); })]]
                    + SVerticalBox::Slot().AutoHeight()[Label(TEXT("본문과 확인 방법·결과를 복사합니다. 선택한 대화 원문을 자동으로 덧붙이지 않아요. 본문에 남은 인용문도 공개해도 되는지 확인하세요."))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 12)[SNew(SCheckBox).IsEnabled_Lambda(CanConfirm)
                        .IsChecked_Lambda([this] { return PortfolioRecord.bConfirmed ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { PortfolioRecord.bConfirmed = State == ECheckBoxState::Checked; bPortfolioDirty = true; })
                        [Label(TEXT("사실과 내 기여를 확인했고, 개인정보·비공개 내용을 제거했어요."), TEXT("Tutor.Strong"))]]
                    + SVerticalBox::Slot().AutoHeight()[Label(TEXT("내용이나 선택한 대화를 바꾸면 다시 확인해야 합니다."))]]]]
        + SVerticalBox::Slot().AutoHeight().Padding(16, 8, 16, 12)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))
                .Text_Lambda([this] { return bPortfolioDirty ? LOCTEXT("Unsaved", "저장하지 않은 변경 사항이 있어요") : PortfolioRecord.Revision.IsEmpty() ? LOCTEXT("NewRecord", "새 기록 · 아직 저장하지 않았어요") : LOCTEXT("Saved", "변경 사항이 저장되어 있어요"); })]
            + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6))
                + SWrapBox::Slot()[SNew(SBox).Visibility_Lambda([this] { return PortfolioStep == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    [StyledButton(LOCTEXT("Draft", "내 설명 작성하기"), TEXT("Tutor.PrimaryButton"), [this] { GeneratePortfolio(); }, [this] { return PortfolioAllowed() && !PortfolioSelection.IsEmpty(); })]]
                + SWrapBox::Slot()[SNew(SBox).Visibility_Lambda([this] { return PortfolioStep == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                    [StyledButton(LOCTEXT("Review", "공유할 내용 확인"), TEXT("Tutor.PrimaryButton"), [this] { PortfolioStep = 2; PortfolioMessage = TEXT("미리보기를 읽고 사실·기여·공개 범위를 확인한 뒤 ‘기록 복사’를 누르세요."); }, CanConfirm)]]
                + SWrapBox::Slot()[SNew(SBox).Visibility_Lambda([this] { return PortfolioStep == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                    [StyledButton(LOCTEXT("Copy", "기록 복사"), TEXT("Tutor.PrimaryButton"), [this] { FString Text; if (PortfolioExport(Text)) { FPlatformApplicationMisc::ClipboardCopy(*Text); PortfolioMessage = TEXT("기록을 저장하고 복사했어요. 보고서나 포트폴리오에 붙여 넣을 수 있어요."); } }, [this, CanConfirm] { return CanConfirm() && PortfolioRecord.bConfirmed; })]]
                + SWrapBox::Slot()[Button(LOCTEXT("Save", "기록 저장"), [this] { SavePortfolio(); }, [this] { return PortfolioAllowed() && !PortfolioSelection.IsEmpty(); })]
                + SWrapBox::Slot()[Button(LOCTEXT("Original", "대화로 돌아가기"), [this] { OpenConversation(PortfolioConversation.Id); }, [this] { return !Busy(); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SBox).HAlign(HAlign_Left)
                .Visibility_Lambda([this] { return bPortfolioDirty ? EVisibility::Visible : EVisibility::Collapsed; })
                [StyledButton(LOCTEXT("Discard", "변경 취소·저장한 상태로 돌아가기"), TEXT("Tutor.GhostButton"), [this] { bPortfolioDirty = false; OpenPortfolio(PortfolioConversation.Id); })]]];
}
#undef LOCTEXT_NAMESPACE

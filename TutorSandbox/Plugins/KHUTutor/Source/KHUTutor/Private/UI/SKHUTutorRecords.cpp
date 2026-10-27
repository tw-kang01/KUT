#include "UI/SKHUTutorPanel.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KHUTutorRecords"

namespace
{
    constexpr int32 RecordsPerPage = 20;

    FString RecordScopeLabel(const FString& Scope, const FString& CurrentScope)
    {
        if (Scope.IsEmpty()) { return Scope == CurrentScope ? TEXT("일반 질문 · 현재 작업") : TEXT("일반 질문 · 읽기 전용"); }
        FString Name = Scope;
        Scope.Split(TEXT("."), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        return Name + (Scope == CurrentScope ? TEXT(" · 현재 Blueprint") : TEXT(" · 다른 Blueprint · 읽기 전용"));
    }

}

void SKHUTutorPanel::OpenRecords()
{
    if (bStopped) { return; }
    RefreshRecords();
    ShowPage(4);
}

TSharedRef<SWidget> SKHUTutorPanel::BuildRecordsPage()
{
    auto Actions = SNew(SWrapBox).UseAllottedSize(true);
    Actions->AddSlot().Padding(0, 0, 8, 8)
        [StyledButton(LOCTEXT("RecordCurrent", "현재 채팅 기록하기"), TEXT("Tutor.PrimaryButton"),
            [this] { OpenPortfolio(ActiveConversation); },
            [this] { return bCanRecordActive && !bStopped && !bMock && !Busy(); }, nullptr,
            LOCTEXT("RecordCurrentTip", "현재 채팅에서 남길 내용을 고르고, 문제를 해결한 과정을 내 말로 작성해요."))];
    Actions->AddSlot().Padding(0, 0, 0, 8)
        [StyledButton(LOCTEXT("Refresh", "새로고침"), TEXT("Tutor.SecondaryButton"),
            [this] { RefreshRecords(); }, [this] { return !bStopped; })];

    return SNew(SScrollBox) + SScrollBox::Slot().Padding(16)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [SNew(STextBlock).Text(LOCTEXT("Title", "학습 기록")).AutoWrapText(true)
                    .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Section")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [SNew(STextBlock).Text(LOCTEXT("Purpose", "문제와 시도, 확인한 결과를 다른 사람에게 설명할 수 있게 남겨요."))
                    .AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
                [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Notice.Info"))).Padding(12)
                    .Visibility_Lambda([this] { return bPortfolioDirty && !PortfolioConversation.Id.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock).Text(LOCTEXT("UnsavedRecord", "저장하지 않은 학습 기록"))
                                .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 6)
                            [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FKHUPortfolio::DisplayTitle(PortfolioConversation)); })
                                .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                            [SNew(STextBlock).Text(LOCTEXT("UnsavedRecordHelp", "작성한 내용은 이 창에 남아 있어요. 계속 작성한 뒤 저장하세요."))
                                .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
                            [StyledButton(LOCTEXT("ResumeUnsavedRecord", "이어서 작성"), TEXT("Tutor.PrimaryButton"),
                                [this] { ShowPage(3); }, [this] { return !bStopped && !bMock && !Busy(); })]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
                [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Inset"))).Padding(12)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                            [SNew(STextBlock).Text_Lambda([this]
                                { return FText::FromString(ActiveConversation.IsEmpty() ? TEXT("채팅에서 시작하세요")
                                    : TEXT("현재 채팅 · ") + (ActiveConversationTitle.IsEmpty() ? TEXT("새 채팅") : ActiveConversationTitle)); })
                                .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]
                        + SVerticalBox::Slot().AutoHeight()[Actions]
                        + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock).Text_Lambda([this]
                                { return FText::FromString(bCanRecordActive
                                    ? TEXT("대화 선택 → 내 설명 작성 → 확인하고 저장. AI의 도움은 필요할 때 요청할 수 있어요.")
                                    : TEXT("현재 작업에서 질문을 주고받은 채팅을 열면 기록을 작성할 수 있어요.")); })
                                .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(STextBlock).Text(FText::FromString(FString(FApp::GetProjectName()) + TEXT(" · 저장한 기록")))
                    .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(SEditableTextBox).Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput")))
                    .HintText(LOCTEXT("Search", "기록 제목 검색")).Text_Lambda([this] { return FText::FromString(RecordQuery); })
                    .OnTextChanged_Lambda([this](const FText& Text) { RecordQuery = Text.ToString(); RecordPageStart = 0; RefreshRecords(); })]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(RecordsMessage); })
                    .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))]
            + SVerticalBox::Slot().AutoHeight()[SAssignNew(RecordList, SVerticalBox)]];
}

void SKHUTutorPanel::RefreshRecords()
{
    bCanRecordActive = false;
    if (!RecordList || bStopped) { return; }
    RecordList->ClearChildren();
    if (!ProjectMemory || bMock)
    { RecordsMessage = TEXT("프로젝트의 학습 기록을 사용할 수 없어요. 실제 채팅 모드에서 다시 열어 주세요."); return; }

    TArray<FKHULearningFolder> Folders;
    TArray<FKHUSavedConversation> Conversations;
    if (!ProjectMemory->ListConversations(Folders, Conversations)) { RecordsMessage = ProjectMemory->Error(); return; }

    FKHUSavedConversation Active;
    if (!ActiveConversation.IsEmpty() && ProjectMemory->ReadConversation(ActiveConversation, Hint, Active))
    {
        ActiveConversationTitle = FKHUPortfolio::DisplayTitle(Active);
        bCanRecordActive = Active.Scope == AssetScope() && !Active.bArchived && !Active.bPortfolioHidden && !Active.Turns.IsEmpty();
    }
    // Metadata selects the page before full records are read. Hidden higher-hint
    // bodies never enter the Slate tree, and long projects load at most 20 rows.
    TArray<FKHUSavedConversation> Matching;
    for (const auto& C : Conversations)
    { if (C.bHasPortfolio && (RecordQuery.IsEmpty() || C.Title.Contains(RecordQuery))) { Matching.Add(C); } }
    if (Matching.IsEmpty())
    {
        RecordPageStart = 0;
        RecordsMessage = RecordQuery.IsEmpty()
            ? TEXT("아직 저장한 기록이 없어요. 현재 채팅에서 기록을 작성하면 여기에 모여요.")
            : TEXT("일치하는 기록이 없어요. 검색어를 바꿔 주세요.");
        return;
    }
    RecordPageStart = FMath::Clamp(RecordPageStart, 0, ((Matching.Num() - 1) / RecordsPerPage) * RecordsPerPage);
    const int32 End = FMath::Min(RecordPageStart + RecordsPerPage, Matching.Num());
    RecordsMessage = FString::Printf(TEXT("저장한 기록 %d개 · %d–%d번째"), Matching.Num(), RecordPageStart + 1, End);
    for (int32 I = RecordPageStart; I < End; ++I)
    {
        FKHUSavedConversation C;
        if (!ProjectMemory->ReadConversation(Matching[I].Id, Hint, C))
        { RecordsMessage = TEXT("일부 기록을 읽지 못했어요. 새로고침해 주세요. ") + ProjectMemory->Error(); continue; }
        const bool bHidden = C.bPortfolioHidden;
        if (!bHidden && !C.Portfolio) { continue; }
        const auto Record = bHidden ? FKHUPortfolio() : FKHUPortfolio::Read(C.Portfolio);
        const bool bEditable = !bHidden && !C.bArchived && C.Scope == AssetScope();
        FString Detail = RecordScopeLabel(C.Scope, AssetScope());
        if (C.bArchived) { Detail += TEXT(" · 보관됨 · 읽기 전용"); }
        FDateTime Updated;
        if (FDateTime::ParseIso8601(*C.Updated, Updated))
        { Detail += TEXT("\n대화 날짜 · ") + FText::AsDateTime(Updated, EDateTimeStyle::Short, EDateTimeStyle::Short).ToString(); }
        const FString DisplayBody = bHidden ? TEXT("") : FKHUPortfolio::DisplayQuestion(Record.Body);
        FString Preview = bHidden ? TEXT("현재 도움 수준보다 자세한 내용이 포함되어 있어 본문을 숨겼어요.")
            : FKHUProjectMemory::Excerpt(DisplayBody, 140).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" "));
        if (Preview.IsEmpty()) { Preview = TEXT("아직 본문을 작성하지 않았어요."); }
        auto Row = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text(FText::FromString(FKHUPortfolio::DisplayTitle(C)))
                    .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 6)
                [SNew(STextBlock).Text(FText::FromString(bHidden ? TEXT("도움 수준 확인 필요") : Record.bConfirmed ? TEXT("확인 완료") : TEXT("작성 중")))
                    .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.AccentLabel")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(STextBlock).Text(FText::FromString(Preview)).AutoWrapText(true).LineHeightPercentage(1.4f)
                    .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(STextBlock).Text(FText::FromString(Detail)).AutoWrapText(true)
                    .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))];
        if (!bHidden)
        {
            const FString FullText = DisplayBody + (Record.Evidence.IsEmpty() ? TEXT("") : TEXT("\n\n확인 방법과 결과\n") + FKHUPortfolio::DisplayQuestion(Record.Evidence));
            Row->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(SExpandableArea).InitiallyCollapsed(true)
                    .BorderImage(Style->GetBrush(TEXT("Tutor.Inset"))).BodyBorderImage(Style->GetBrush(TEXT("Tutor.Inset")))
                    .HeaderContent()[SNew(STextBlock).Text(LOCTEXT("ReadRecord", "전체 기록 읽기"))
                        .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.AccentLabel")))]
                    .BodyContent()[SNew(SBox).HeightOverride(220)
                        [SNew(SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true).Text(FText::FromString(FullText))
                            .Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Tutor.InputText")))
                            .Font(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).Font)
                            .ReadOnlyForegroundColor(Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")).ColorAndOpacity)]]];
        }
        auto Actions = SNew(SWrapBox).UseAllottedSize(true);
        if (bEditable)
        {
            Actions->AddSlot().Padding(0, 0, 8, 0)
                [StyledButton(LOCTEXT("EditRecord", "기록 이어 쓰기"), TEXT("Tutor.SecondaryButton"),
                    [this, C] { OpenPortfolio(C.Id); }, [this] { return !bStopped && !bMock && !Busy(); })];
        }
        Actions->AddSlot()
            [StyledButton(LOCTEXT("ReadConversation", "원래 채팅 보기"), TEXT("Tutor.GhostButton"),
                [this, C] { OpenConversation(C.Id); }, [this] { return !bStopped && !bMock && !Busy(); })];
        Row->AddSlot().AutoHeight()[Actions];
        if (!bHidden && !bEditable)
        {
            Row->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
                [SNew(STextBlock).Text(C.bArchived
                    ? LOCTEXT("ArchivedRecord", "보관한 채팅의 기록이에요. 채팅·폴더 관리에서 복원하면 수정할 수 있어요.")
                    : LOCTEXT("OtherScopeRecord", "이 기록은 읽기 전용이에요. 원래 작업의 KHU Tutor에서 이어 쓸 수 있어요."))
                    .AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))];
        }
        RecordList->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
            [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Settings.Card"))).Padding(14)[Row]];
    }
    if (Matching.Num() > RecordsPerPage)
    {
        auto Paging = SNew(SWrapBox).UseAllottedSize(true);
        Paging->AddSlot().Padding(0, 0, 8, 8)
            [StyledButton(LOCTEXT("PreviousRecords", "이전 기록"), TEXT("Tutor.SecondaryButton"),
                [this] { RecordPageStart = FMath::Max(0, RecordPageStart - RecordsPerPage); RefreshRecords(); },
                [this] { return RecordPageStart > 0; })];
        Paging->AddSlot().Padding(0, 0, 0, 8)
            [StyledButton(LOCTEXT("NextRecords", "다음 기록"), TEXT("Tutor.SecondaryButton"),
                [this] { RecordPageStart += RecordsPerPage; RefreshRecords(); }, [bHasMore = End < Matching.Num()] { return bHasMore; })];
        RecordList->AddSlot().AutoHeight()[Paging];
    }
}

#undef LOCTEXT_NAMESPACE

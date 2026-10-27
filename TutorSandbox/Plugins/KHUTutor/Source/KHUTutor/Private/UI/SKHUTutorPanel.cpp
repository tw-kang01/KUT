#include "UI/SKHUTutorPanel.h"
#include "UI/KHUEditorObservation.h"
#include "UI/KHUFeedbackStore.h"
#include "BlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraph.h"
#include "UI/KHUTutorSettings.h"
#include "UI/KHUTutorStyle.h"
#include "UI/KHUConversationText.h"
#include "UI/SKHUSelectableText.h"
#include "KHUTutorModule.h"
#include "AI/KHUSecretStore.h"
#include "AI/KHUClaudeRuntime.h"
#include "AI/KHUCppPractice.h"
#include "Digest/KHUGraphDigest.h"
#include "Digest/KHUNodeRefCache.h"
#include "EdGraph/EdGraphNode.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "KHUTutorPanel"
namespace
{
    FKHUSecretStore Secret() { return FKHUSecretStore(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi")); }
    void Preferences(bool Language)
    { FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings")).ShowViewer(TEXT("Editor"), Language ? TEXT("General") : TEXT("Plugins"), Language ? TEXT("Internationalization") : TEXT("KHUTutorSettings")); }
    using KHUTutorPalette::Hex;
}

// ---------------------------------------------------------------- building blocks
TSharedRef<SWidget> SKHUTutorPanel::Button(const FText& Label, TFunction<void()> Action, TFunction<bool()> Enabled)
{
    return StyledButton(Label, TEXT("Tutor.SecondaryButton"), MoveTemp(Action), MoveTemp(Enabled));
}
TSharedRef<SWidget> SKHUTutorPanel::StyledButton(const FText& Label, FName Appearance, TFunction<void()> Action, TFunction<bool()> Enabled, const FSlateBrush* Icon, const FText& Tip)
{
    const bool Primary = Appearance == TEXT("Tutor.PrimaryButton"), Ghost = Appearance == TEXT("Tutor.GhostButton"), Chip = Appearance == TEXT("Tutor.ChipButton");
    const FName TextStyle = Primary ? FName(TEXT("Tutor.PrimaryText")) : Ghost ? FName(TEXT("Tutor.GhostText")) : FName(TEXT("Tutor.ButtonText"));
    auto Content = SNew(SHorizontalBox);
    if (Icon)
    {
        Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
            [SNew(SImage).Image(Icon).ColorAndOpacity(Primary ? FSlateColor(FLinearColor::White) : FSlateColor(KHUTutorPalette::Icon()))];
    }
    Content->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(Label).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TextStyle))];
    return SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(Appearance))
        .ContentPadding(Chip ? FMargin(11, 5) : Ghost ? FMargin(8, 5) : FMargin(12, 7)).ToolTipText(Tip)
        .IsEnabled_Lambda([this, Enabled] { return !bStopped && (!Enabled || Enabled()); })
        .OnClicked_Lambda([Action] { Action(); return FReply::Handled(); })[Content];
}
TSharedRef<SWidget> SKHUTutorPanel::IconButton(const FSlateBrush* Icon, const FText& Tip, TFunction<void()> Action, TFunction<bool()> Enabled, TFunction<FSlateColor()> Tint)
{
    return SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.GhostButton"))).ContentPadding(FMargin(6)).ToolTipText(Tip)
        .IsEnabled_Lambda([this, Enabled] { return !bStopped && (!Enabled || Enabled()); })
        .OnClicked_Lambda([Action] { Action(); return FReply::Handled(); })
        [SNew(SImage).Image(Icon).ColorAndOpacity_Lambda([Tint] { return Tint ? Tint() : FSlateColor(KHUTutorPalette::Icon()); })];
}
TSharedRef<SWidget> SKHUTutorPanel::SettingsButton(const FText& Label, TFunction<void()> Action, FName Appearance, TFunction<bool()> Enabled)
{
    return SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(Appearance)).ContentPadding(FMargin(12, 9))
        .IsEnabled_Lambda([this, Enabled] { return !bStopped && (!Enabled || Enabled()); })
        .OnClicked_Lambda([Action] { Action(); return FReply::Handled(); })
        [SNew(STextBlock).Text(Label).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(
            Appearance == TEXT("Settings.DangerButton") ? TEXT("Settings.DangerText") : Appearance == TEXT("Settings.PrimaryButton") ? TEXT("Tutor.PrimaryText") : TEXT("Settings.Body")))];
}
FText SKHUTutorPanel::HintName(int32 Level)
{
    switch (FMath::Clamp(Level, 0, 3))
    {
    case 0: return LOCTEXT("HintNameZero", "질문으로 생각하기");
    case 1: return LOCTEXT("HintNameOne", "위치 힌트");
    case 2: return LOCTEXT("HintNameTwo", "부분 단계");
    default: return LOCTEXT("HintNameThree", "전체 단계");
    }
}
FText SKHUTutorPanel::HintDescription(int32 Level)
{
    switch (FMath::Clamp(Level, 0, 3))
    {
    case 0: return LOCTEXT("SettingsHintZeroBody", "답을 받기 전에 스스로 생각할 질문을 받아요.");
    case 1: return LOCTEXT("SettingsHintOneBody", "어느 메뉴나 노드를 확인하면 좋을지 알려줘요.");
    case 2: return LOCTEXT("SettingsHintTwoBody", "필요한 부분의 순서와 이유를 설명해요.");
    default: return LOCTEXT("SettingsHintThreeBody", "처음부터 끝까지 순서대로 설명해요.");
    }
}

// ---------------------------------------------------------------- settings page
TSharedRef<SWidget> SKHUTutorPanel::BuildSettingsPage()
{
    const auto Text = [this](const FText& Content, FName TextStyle = TEXT("Settings.Body"))
    { return SNew(STextBlock).Text(Content).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TextStyle)).AutoWrapText(true).LineHeightPercentage(1.4f); };
    const auto Card = [this](TSharedRef<SWidget> Content)
    { return SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Settings.Card"))).Padding(16)[Content]; };
    const auto Step = [this, &Text](int32 Number, const FText& Title, const FText& Description)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(26).HeightOverride(26)
                [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Settings.Step"))).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [Text(FText::AsNumber(Number), TEXT("Tutor.AccentLabel"))]]]
            + SHorizontalBox::Slot().FillWidth(1).Padding(10, 1, 0, 0)[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Text(Title, TEXT("Settings.Label"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Text(Description, TEXT("Settings.Caption"))]];
    };
    const auto Available = [this] { return !Busy() && !bDiagnosing; };
    auto ConnectActions = SNew(SWrapBox).UseAllottedSize(true);
    ConnectActions->AddSlot().Padding(0, 0, 8, 8)[SettingsButton(LOCTEXT("SettingsSaveConnect", "키 저장하고 연결"), [this]
    {
        FString Error;
        const bool Ok = Secret().Save(KeyInput->GetText().ToString().TrimStartAndEnd(), Error);
        KeyInput->SetText(FText::GetEmpty()); bApiConnected = false; KeyStatus = LOCTEXT("Unchecked", "미확인").ToString(); Balance.Reset();
        ConnectionMessage = Ok ? LOCTEXT("SettingsSavedKey", "키를 안전하게 저장했어요. 연결을 확인하고 있어요.").ToString() : Error;
        if (Ok) { ResetTask(false); Diagnose(); }
    }, TEXT("Settings.PrimaryButton"), [this] { return !Busy() && !bDiagnosing && KeyInput && !KeyInput->GetText().ToString().TrimStartAndEnd().IsEmpty(); })];
    ConnectActions->AddSlot().Padding(0, 0, 8, 8)[SettingsButton(LOCTEXT("SettingsRecheck", "저장된 키로 연결 확인"), [this] { Diagnose(); }, TEXT("Settings.SecondaryButton"), Available)];
    ConnectActions->AddSlot().Padding(0, 0, 0, 8)[SettingsButton(LOCTEXT("SettingsDeleteKey", "키 삭제"), [this]
    {
        FString Error; const bool Ok = Secret().Delete(Error);
        KeyInput->SetText(FText::GetEmpty()); bApiConnected = false; KeyStatus = LOCTEXT("Unchecked", "미확인").ToString(); Balance.Reset();
        if (Ok) { ResetTask(false); }
        ConnectionMessage = Ok ? LOCTEXT("SettingsDeletedKey", "저장된 키를 삭제했어요. 새 키를 입력하거나 환경변수의 키로 연결을 확인해 주세요.").ToString() : Error;
    }, TEXT("Settings.DangerButton"), Available)];
    auto Connection = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(LOCTEXT("SettingsConnection", "ChatKHU 연결"), TEXT("Settings.Section"))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Badge"))).Padding(FMargin(8, 2))
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Dot")))
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(bDiagnosing ? KHUTutorPalette::Warning() : bApiConnected ? KHUTutorPalette::Success() : KHUTutorPalette::Error()); })]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Badge")))
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(bApiConnected && !bDiagnosing ? KHUTutorPalette::Accent() : Hex(TEXT("EDEDED"))); })
                        .Text_Lambda([this] { return bDiagnosing ? LOCTEXT("SettingsChecking", "연결 확인 중") : bApiConnected ? LOCTEXT("SettingsConnected", "연결됨") : LOCTEXT("SettingsNotConnected", "연결 필요"); })]]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 8)[Text(LOCTEXT("SettingsConnectionIntro", "ChatKHU에 연결하면 Claude가 이 프로젝트의 대화를 이어서 도와줘요."), TEXT("Settings.Caption"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)[SNew(STextBlock).Text_Lambda([this] { return FKHUClaudeRuntime::Executable(Config).IsEmpty()
            ? LOCTEXT("ClaudeMissing", "Claude 연결 프로그램: 준비 필요 · 고급 설정에서 claude.exe 위치를 확인하세요.")
            : LOCTEXT("ClaudeReady", "Claude 연결 프로그램: 준비됨 · 에디터 도구 연결은 질문할 때 자동으로 준비해요."); }).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))]
        + SVerticalBox::Slot().AutoHeight()[Step(1, LOCTEXT("SettingsStepOne", "ChatKHU에서 키 발급"), LOCTEXT("SettingsStepOneHint", "로그인 → API Gateway → API 키 생성"))]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(36, 10, 0, 20)[SettingsButton(LOCTEXT("SettingsKeyGuide", "키 발급 안내 열기"), []
            { FPlatformProcess::LaunchURL(TEXT("https://docs.mindlogic.ai/docs/khu/api-gateway/getting-started/authentication"), nullptr, nullptr); }, TEXT("Settings.SecondaryButton"))]
        + SVerticalBox::Slot().AutoHeight()[Step(2, LOCTEXT("SettingsStepTwo", "API 키 입력"), LOCTEXT("SettingsStepTwoHint", "API 키는 ChatKHU를 사용할 수 있는 개인 인증 정보예요."))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 7)[SAssignNew(KeyInput, SEditableTextBox)
            .Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Settings.KeyInput"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11))
            .IsPassword(true).HintText(LOCTEXT("SettingsKeyPlaceholder", "복사한 API 키를 붙여 넣으세요"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)[Text(LOCTEXT("SettingsKeyPrivacy", "이 PC에 암호화해 저장해요. 질문 칸에는 붙여 넣지 마세요."), TEXT("Settings.Caption"))]
        + SVerticalBox::Slot().AutoHeight()[Step(3, LOCTEXT("SettingsStepThree", "저장하고 연결 확인"), LOCTEXT("SettingsStepThreeHint", "키를 저장한 뒤 연결 상태를 자동으로 확인해요."))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 4)[ConnectActions]
        + SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Settings.Inset"))).Padding(12)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).LineHeightPercentage(1.4f)
                    .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Body"))).Text_Lambda([this] { return FText::FromString(ConnectionMessage); })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))
                    .Visibility_Lambda([this] { return bApiConnected ? EVisibility::Visible : EVisibility::Collapsed; })
                    .Text_Lambda([this] { return Balance.IsSet() ? FText::Format(LOCTEXT("SettingsBalance", "조회 잔액: {0} 크레딧 · 마지막 연결 확인 시점 · 학생 모델 {1}"), FText::AsNumber(Balance.GetValue()), FText::FromString(ModelLabel()))
                        : LOCTEXT("SettingsBalanceUnknown", "잔액은 아직 확인하지 못했어요."); })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 4)[Text(LOCTEXT("SettingsTutorInstructions", "튜터의 기본 지침은 질문할 때 자동으로 적용돼요."), TEXT("Settings.Caption"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SExpandableArea).InitiallyCollapsed(true)
            .HeaderContent()[Text(LOCTEXT("SettingsConnectionDetails", "연결 상세 보기"), TEXT("Settings.Caption"))]
            .BodyContent()[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 6)[SNew(STextBlock).Text(this, &SKHUTutorPanel::Header).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))]
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Diagnostics); }).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))]]];
    auto Help = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(LOCTEXT("SettingsLearning", "학습 도움"), TEXT("Settings.Section"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 14)[Text(LOCTEXT("SettingsLearningIntro", "얼마나 자세한 설명을 받을지 선택하세요. 대화 중에는 입력창 아래의 도움 수준 버튼에서도 바꿀 수 있어요."), TEXT("Settings.Caption"))];
    for (int32 Level = 0; Level < 4; ++Level)
    {
        Help->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SNew(SCheckBox).Style(&Style->GetWidgetStyle<FCheckBoxStyle>(TEXT("Settings.Choice")))
            .IsChecked_Lambda([this, Level] { return Hint == Level ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .IsEnabled_Lambda([this] { return !Busy(); }).OnCheckStateChanged_Lambda([this, Level](ECheckBoxState) { ChangeHint(Level, false); })
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Text(Level == 1 ? FText::Format(LOCTEXT("SettingsHintRecommended", "L{0} {1} · 추천"), FText::AsNumber(Level), HintName(Level))
                    : FText::Format(LOCTEXT("SettingsHintTitle", "L{0} {1}"), FText::AsNumber(Level), HintName(Level)), TEXT("Settings.Label"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Text(HintDescription(Level), TEXT("Settings.Caption"))]]];
    }
    auto Attachment = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(LOCTEXT("SettingsAttachment", "질문에 첨부할 내용"), TEXT("Settings.Section"))];
    if (bBlueprintPanel)
    {
        Attachment->AddSlot().AutoHeight().Padding(0, 6, 0, 14)[Text(LOCTEXT("SettingsAttachmentIntro", "Blueprint의 어느 범위를 함께 확인할지 선택하세요. 대화 화면 위쪽의 대상 표시에서도 바꿀 수 있어요."), TEXT("Settings.Caption"))];
        for (bool Selected : {true, false})
        {
            Attachment->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SNew(SCheckBox).Style(&Style->GetWidgetStyle<FCheckBoxStyle>(TEXT("Settings.Choice")))
                .IsChecked_Lambda([this, Selected] { return bSelectionOnly == Selected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .IsEnabled_Lambda([this] { return !Busy() && !bHasTask; }).OnCheckStateChanged_Lambda([this, Selected](ECheckBoxState) { bSelectionOnly = Selected; })
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Text(Selected ? LOCTEXT("SettingsSelectedNodes", "선택한 노드") : LOCTEXT("SettingsCurrentGraph", "현재 Graph 요약"), TEXT("Settings.Label"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Text(Selected ? LOCTEXT("SettingsSelectedNodesBody", "직접 선택한 노드를 중심으로 살펴봐요.") : LOCTEXT("SettingsCurrentGraphBody", "지금 열려 있는 Graph의 흐름을 함께 살펴봐요."), TEXT("Settings.Caption"))]]];
        }
        Attachment->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Text(this, &SKHUTutorPanel::CompactContextLabel).ToolTipText(this, &SKHUTutorPanel::ContextLabel).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption")))];
        Attachment->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(LOCTEXT("SettingsScopeLocked", "대화가 진행 중이면 새 대화에서 첨부 범위를 변경할 수 있어요."), TEXT("Settings.Caption"))];
    }
    else
    {
        Attachment->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Text(LOCTEXT("SettingsGeneralQuestion", "지금은 일반 질문을 하고 있어요."))];
        Attachment->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(LOCTEXT("SettingsOpenBlueprint", "Blueprint를 함께 보려면 해당 Blueprint를 열고 Window → KHU Tutor를 선택하세요."), TEXT("Settings.Caption"))];
    }
    auto Language = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(LOCTEXT("SettingsLanguage", "언리얼 표시 언어"), TEXT("Settings.Section"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 12)[Text(LOCTEXT("SettingsLanguageIntro", "언리얼 메뉴의 표시 언어를 변경합니다."), TEXT("Settings.Caption"))]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[SettingsButton(LOCTEXT("SettingsLanguageButton", "Region & Language 열기"), [] { Preferences(true); }, TEXT("Settings.SecondaryButton"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SNew(STextBlock).Text(LOCTEXT("SettingsEnglishMenus", "튜터의 안내는 English 메뉴 이름을 기준으로 해요."))
            .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Settings.Caption"))).AutoWrapText(true).LineHeightPercentage(1.4f)
            .Visibility_Lambda([] { return GetDefault<UKHUTutorSettings>()->bShowLanguageBanner && FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() != TEXT("en") ? EVisibility::Visible : EVisibility::Collapsed; })];
    auto Advanced = SNew(SExpandableArea).InitiallyCollapsed(true)
        .HeaderContent()[Text(LOCTEXT("SettingsAdvanced", "고급 설정 · 개발 테스트"), TEXT("Settings.Label"))]
        .BodyContent()[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 14, 0, 14)[SettingsButton(LOCTEXT("SettingsAdvancedButton", "고급 설정 열기"), [] { Preferences(false); }, TEXT("Settings.SecondaryButton"))]
            + SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsEnabled_Lambda([this] { return !Busy(); })
                .IsChecked_Lambda([this] { return bMock ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { ResetTask(false); bMock = State == ECheckBoxState::Checked; })
                [Text(LOCTEXT("SettingsMock", "개발용 예제 사용"), TEXT("Settings.Label"))]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Text(LOCTEXT("SettingsMockBody", "실제 대상 대신 BP_Door_Broken 예제를 사용해요."), TEXT("Settings.Caption"))]];
    return SNew(SScrollBox)
        + SScrollBox::Slot().Padding(14, 14)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Card(Connection)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Card(Help)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Card(Attachment)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Card(Language)]
            + SVerticalBox::Slot().AutoHeight()[Card(Advanced)]];
}

// ---------------------------------------------------------------- chat page
TSharedRef<SWidget> SKHUTutorPanel::BuildHeader()
{
    const auto PageTint = [this](int32 Page) { return TFunction<FSlateColor()>([this, Page] { return FSlateColor(ActivePage == Page ? KHUTutorPalette::Accent() : KHUTutorPalette::Icon()); }); };
    return SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Header"))).Padding(FMargin(6, 5, 4, 5))
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
                [StyledButton(LOCTEXT("ChatNavigation", "목록"), TEXT("Tutor.SecondaryButton"), [this]
                { if (GetCachedGeometry().GetLocalSize().X < 720.0f) { ShowPage(ActivePage == 1 ? 0 : 1); }
                  else { bSessionSidebarVisible = !bSessionSidebarVisible; RefreshProjects(); } }, {}, nullptr, LOCTEXT("ChatNavigationTip", "이 프로젝트의 채팅 목록 열기 또는 닫기"))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).Visibility_Lambda([this] { return ActivePage == 0 ? EVisibility::Collapsed : EVisibility::Visible; })
                [IconButton(FAppStyle::GetBrush(TEXT("Icons.ArrowLeft")), LOCTEXT("BackToChat", "뒤로"), [this] { if (ActivePage == 3) { OpenRecords(); } else { ShowPage(0); } })]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 8, 0)
                [SNew(SImage).Image(Style->GetBrush(TEXT("KHUMark"))).ToolTipText(LOCTEXT("KHUMarkTip", "경희대학교"))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("AppTitle", "KHU Tutor")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.AppTitle")))]
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(this, &SKHUTutorPanel::HeaderSubtitle).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .ToolTipText(this, &SKHUTutorPanel::HeaderSubtitle).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Subtitle")))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[StyledButton(LOCTEXT("NewChat", "새 채팅"), TEXT("Tutor.GhostButton"), [this] { NewConversation(ActiveFolder); }, [this] { return !Busy(); })]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[IconButton(FAppStyle::GetBrush(TEXT("Icons.Settings")), LOCTEXT("SettingsTip", "설정 · 연결과 학습 도움"), [this] { ShowPage(ActivePage == 2 ? 0 : 2); }, {}, PageTint(2))]];
}
FText SKHUTutorPanel::HeaderSubtitle() const
{
    if (ActivePage == 3 || ActivePage == 4) { return LOCTEXT("SubtitlePortfolio", "학습 기록 · 내 문제 해결 과정과 배운 점"); }
    if (ActivePage == 1) { return FText::Format(LOCTEXT("SubtitleRecords", "채팅 · {0}"), FText::FromString(FApp::GetProjectName())); }
    if (ActivePage == 2) { return LOCTEXT("SubtitleSettings", "설정 · 작성 중인 질문과 대화는 그대로 유지돼요"); }
    if (bMock) { return LOCTEXT("SubtitleMock", "개발 Mock 테스트"); }
    return ActiveConversationTitle.IsEmpty() || ActiveConversationTitle == TEXT("새 대화") ? LOCTEXT("SubtitleNew", "새 채팅") : FText::FromString(ActiveConversationTitle);
}
TSharedRef<SWidget> SKHUTutorPanel::BuildContextBar()
{
    const auto Stale = [this] { return Observation.IsValid() && !Observation->IsCurrent(); };
    return SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.ContextBar"))).Padding(FMargin(12, 6))
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SComboButton)
                .ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.ChipButton"))).HasDownArrow(false).ContentPadding(FMargin(9, 4))
                .ToolTipText(this, &SKHUTutorPanel::ContextLabel)
                .OnGetMenuContent(FOnGetContent::CreateSP(this, &SKHUTutorPanel::ContextMenu))
                .ButtonContent()[SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SBox).WidthOverride(13).HeightOverride(13)
                        [SNew(SImage).Image_Lambda([this, Stale] { return Stale() ? FAppStyle::GetBrush(TEXT("Icons.Warning")) : Style->GetBrush(TEXT("Icon.Eye")); })
                            .ColorAndOpacity_Lambda([Stale] { return FSlateColor(Stale() ? KHUTutorPalette::Warning() : KHUTutorPalette::Accent()); })]]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SKHUTutorPanel::CompactContextLabel)
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.ButtonText")))]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(SBox).WidthOverride(12).HeightOverride(12)
                        [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ChevronDown"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Faint()))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SKHUTutorPanel::ContextState)
                .ColorAndOpacity_Lambda([Stale] { return FSlateColor(Stale() ? KHUTutorPalette::Warning() : KHUTutorPalette::Secondary()); }).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(SBox).Visibility_Lambda([this, Stale] { return Stale() && bBlueprintPanel ? EVisibility::Visible : EVisibility::Collapsed; })
                [StyledButton(LOCTEXT("ReadAgain", "다시 읽기"), TEXT("Tutor.SecondaryButton"), [this]
                { if (!Busy() && CaptureContext()) { Status = LOCTEXT("ReadAgainDone", "그래프를 다시 읽었어요. 다음 질문에 새 근거를 보내요.").ToString(); } }, [this] { return !Busy(); })]]];
}
FText SKHUTutorPanel::ContextState() const
{
    if (bMock || !bBlueprintPanel) { return FText::GetEmpty(); }
    if (!Observation) { return LOCTEXT("ContextOnSend", "보낼 때 읽어요"); }
    if (!Observation->IsCurrent()) { return LOCTEXT("ContextChanged", "그래프가 바뀌었어요"); }
    FDateTime Utc;
    if (FDateTime::ParseIso8601(*Observation->ReadAt, Utc))
    { const FDateTime Local = Utc + (FDateTime::Now() - FDateTime::UtcNow()); return FText::Format(LOCTEXT("ContextReadAt", "읽음 {0}"), FText::FromString(Local.ToString(TEXT("%H:%M")))); }
    return LOCTEXT("ContextRead", "읽음");
}
TSharedRef<SWidget> SKHUTutorPanel::ContextMenu()
{
    FMenuBuilder Menu(true, nullptr);
    if (bBlueprintPanel && !bMock)
    {
        Menu.BeginSection(NAME_None, LOCTEXT("ScopeSection", "질문에 함께 볼 범위"));
        for (const bool Selected : {true, false})
        {
            Menu.AddMenuEntry(Selected ? LOCTEXT("SettingsSelectedNodes", "선택한 노드") : LOCTEXT("SettingsCurrentGraph", "현재 Graph 요약"),
                Selected ? LOCTEXT("SettingsSelectedNodesBody", "직접 선택한 노드를 중심으로 살펴봐요.") : LOCTEXT("SettingsCurrentGraphBody", "지금 열려 있는 Graph의 흐름을 함께 살펴봐요."), FSlateIcon(),
                FUIAction(FExecuteAction::CreateSPLambda(this, [this, Selected] { bSelectionOnly = Selected; }),
                    FCanExecuteAction::CreateSPLambda(this, [this] { return !Busy() && !bHasTask; }),
                    FIsActionChecked::CreateSPLambda(this, [this, Selected] { return bSelectionOnly == Selected; })),
                NAME_None, EUserInterfaceActionType::RadioButton);
        }
        Menu.EndSection();
    }
    Menu.BeginSection(NAME_None, LOCTEXT("ContextInfoSection", "지금 보는 대상"));
    Menu.AddWidget(SNew(SBox).MaxDesiredWidth(320).Padding(FMargin(12, 2, 12, 6))[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(this, &SKHUTutorPanel::ContextLabel).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Micro")))
            .Visibility_Lambda([this] { return bBlueprintPanel && bHasTask ? EVisibility::Visible : EVisibility::Collapsed; })
            .Text(LOCTEXT("SettingsScopeLocked", "대화가 진행 중이면 새 대화에서 첨부 범위를 변경할 수 있어요."))]], FText::GetEmpty(), true);
    Menu.EndSection();
    return Menu.MakeWidget();
}
TSharedRef<SWidget> SKHUTutorPanel::HintMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.BeginSection(NAME_None, LOCTEXT("HintSection", "도움 수준 · 다음 질문부터 적용"));
    for (int32 Level = 0; Level < 4; ++Level)
    {
        Menu.AddMenuEntry(FText::Format(LOCTEXT("HintEntry", "L{0} {1}"), FText::AsNumber(Level), HintName(Level)), HintDescription(Level), FSlateIcon(),
            FUIAction(FExecuteAction::CreateSPLambda(this, [this, Level] { ChangeHint(Level, false); }),
                FCanExecuteAction::CreateSPLambda(this, [this] { return !Busy(); }),
                FIsActionChecked::CreateSPLambda(this, [this, Level] { return Hint == Level; })),
            NAME_None, EUserInterfaceActionType::RadioButton);
    }
    Menu.EndSection();
    return Menu.MakeWidget();
}
TSharedRef<SWidget> SKHUTutorPanel::HelperMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.AddSubMenu(LOCTEXT("CppPractice", "C++/CS 실습"), LOCTEXT("CppPracticeTip", "과제를 입력창에 넣어요. 확인하고 보내세요."),
        FNewMenuDelegate::CreateSP(this, &SKHUTutorPanel::AddPracticeEntries));
    const auto Entry = [this, &Menu](const FText& Label, const FText& Tip, EQuick Kind, TFunction<bool()> Can)
    {
        Menu.AddMenuEntry(Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateSPLambda(this, [this, Kind] { RunQuick(Kind); }),
            FCanExecuteAction::CreateSPLambda(this, [this, Can] { return !bStopped && !Busy() && !bConversationReadOnly && (!Can || Can()); })));
    };
    Menu.BeginSection(NAME_None, LOCTEXT("HelperSection", "질문 도우미"));
    Entry(LOCTEXT("Errors", "최근 오류 설명"), LOCTEXT("ErrorsTip", "최근 Blueprint·C++ 오류를 읽고 지금 도움 수준으로 설명해요. 바로 보내요."), EQuick::Errors, {});
    Entry(LOCTEXT("Selection", "선택 대상의 역할"), LOCTEXT("SelectionTip", "첨부한 대상의 실행·값 흐름을 설명해요. 바로 보내요."), EQuick::Selection, {});
    Entry(LOCTEXT("Relations", "선택 액터 관계·값"), LOCTEXT("RelationsTip", "선택한 액터의 참조와 필요한 Details 값을 읽어요. 바로 보내요."), EQuick::Relations, {});
    Entry(LOCTEXT("Practice", "한 단계씩 실습"), LOCTEXT("PracticeTip", "내가 직접 확인할 행동을 하나씩 받아요. 바로 보내요."), EQuick::Practice, {});
    Entry(LOCTEXT("Where", "이거 어디 있어?"), LOCTEXT("WhereTip", "입력창에 질문 틀을 넣어요. 보내기 전에 고칠 수 있어요."), EQuick::Where, {});
    Entry(LOCTEXT("ExplainBack", "이해한 내용 설명하기"), LOCTEXT("ExplainBackTip", "입력창에 내 설명 틀을 넣어요. 보내기 전에 채워요."), EQuick::ExplainBack, {});
    Entry(LOCTEXT("Recheck", "수정 후 재확인"), LOCTEXT("RecheckTip", "새 그래프와 최신 Compile 기록을 읽어 이전 근거와 비교해요."), EQuick::Recheck, [this] { return bHasTask; });
    Menu.EndSection();
    Menu.BeginSection(NAME_None, LOCTEXT("ConversationSection", "이번 대화"));
    Menu.AddMenuEntry(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSPLambda(this, [this]
        { return Loop && Loop->LearningCoach().bPaused ? LOCTEXT("CoachResumeMenu", "자동 코칭 다시 켜기") : LOCTEXT("CoachPauseMenu", "자동 코칭 일시 중지"); })),
        LOCTEXT("CoachToggleTip", "질문 돌아보기 제안을 이번 대화에서만 멈추거나 다시 켜요. 일반 도움은 그대로예요."), FSlateIcon(),
        FUIAction(FExecuteAction::CreateSPLambda(this, [this]
        {
            if (!Loop || Busy()) { return; }
            const FString Choice = Loop->LearningCoach().bPaused ? TEXT("resume") : TEXT("pause");
            Status = Loop->ChooseCoaching(Choice, Loop->LearningCoach().Id()) ? TEXT("자동 코칭 선택을 저장했어요. 일반 도움은 계속 이용할 수 있어요.") : TEXT("선택을 저장하지 못했어요. 이전 선택을 유지했어요.");
        }), FCanExecuteAction::CreateSPLambda(this, [this] { return !Busy() && Loop && bHasTask && !bConversationReadOnly; })));
    Menu.AddMenuEntry(LOCTEXT("New", "새 작업으로 문맥 나누기"), LOCTEXT("NewTip", "지금 대화를 기록에 남기고, 다음 질문을 새 작업으로 시작해요."), FSlateIcon(),
        FUIAction(FExecuteAction::CreateSPLambda(this, [this] { ResetTask(false); }), FCanExecuteAction::CreateSPLambda(this, [this] { return !Busy(); })));
    Menu.EndSection();
    return Menu.MakeWidget();
}
TSharedRef<SWidget> SKHUTutorPanel::PracticeMenu()
{
    FMenuBuilder Menu(true, nullptr);
    AddPracticeEntries(Menu);
    return Menu.MakeWidget();
}
void SKHUTutorPanel::AddPracticeEntries(FMenuBuilder& Menu)
{
    for (bool bTransfer : {false, true})
    {
        Menu.BeginSection(NAME_None, bTransfer ? LOCTEXT("CppTransfer", "조건을 바꿔 다시 연습") : LOCTEXT("CppBase", "C++/CS 기본 실습"));
        for (const auto& E : FKHUCppPractice::All())
        {
            const FString Id = E.Id;
            Menu.AddMenuEntry(FText::FromString(E.Title), FText::FromString(bTransfer ? E.Transfer : E.Goal), FSlateIcon(),
                FUIAction(FExecuteAction::CreateSPLambda(this, [this, Id, bTransfer] { DraftPractice(Id, bTransfer); }),
                    FCanExecuteAction::CreateSPLambda(this, [this] { return !bStopped && !Busy() && !bConversationReadOnly; })));
        }
        Menu.EndSection();
    }
}
void SKHUTutorPanel::DraftPractice(const FString& Id, bool bTransfer)
{
    if (bStopped || Busy() || bConversationReadOnly || !Input) { return; }
    const FString Draft = FKHUCppPractice::Draft(Id, bTransfer);
    if (Draft.IsEmpty()) { return; }
    InsertDraft(FText::FromString(Draft), TEXT("실습을 입력창에 넣었어요. 내용을 확인한 뒤 보내세요."));
}
bool SKHUTutorPanel::InsertDraft(const FText& Draft, const FString& Message)
{
    if (bStopped || Busy() || bConversationReadOnly || !Input || Draft.IsEmpty()) { return false; }
    // All draft helpers share the same preservation rule and visible composer notice.
    if (!Input->GetText().ToString().TrimStartAndEnd().IsEmpty())
    {
        bDraftConflict = true;
        Status = TEXT("작성 중인 질문이 있어요. 먼저 보내거나 입력창을 비운 뒤 다시 선택해 주세요. 작성한 내용은 그대로 보존했어요.");
        return false;
    }
    Input->SetText(Draft); bDraftConflict = false; Status = Message;
    if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetKeyboardFocus(Input, EFocusCause::SetDirectly); }
    return true;
}
void SKHUTutorPanel::RunQuick(EQuick Kind)
{
    switch (Kind)
    {
    case EQuick::Errors: Submit(TEXT("최근 Blueprint 또는 C++ 컴파일 오류를 읽고 현재 힌트 레벨로 설명해 주세요.")); break;
    case EQuick::Selection: Submit(TEXT("첨부된 대상의 역할과 실행/값 흐름을 현재 힌트 수준에서 설명해 주세요. 문맥이 없다면 선택한 대상을 도구로 확인해 주세요.")); break;
    case EQuick::Practice: Submit(TEXT("이 문제에서 배울 목표를 짧게 정하고, 현재 힌트 수준에서 제가 직접 확인할 행동 한 가지와 이유를 알려 주세요. 제 관찰을 받은 뒤 다음 단계로 진행해 주세요.")); break;
    case EQuick::Recheck:
        if (bHasTask) { Submit(TEXT("제가 직접 수정했습니다. 새로 첨부한 그래프와 최신 컴파일 기록을 읽고 이전 근거와 비교해 주세요. 아직 Compile하지 않았거나 실행 결과가 없으면 미확인으로 구분하고, 현재 힌트 수준에서 다음 확인 한 가지만 안내해 주세요.")); }
        break;
    case EQuick::Relations: Submit(TEXT("Editor에 선택한 액터의 GetActorRelations를 깊이 1로 읽고 관련 참조를 확인해 주세요. 필요한 Details 값만 GetObjectDetails로 읽어 클래스 기본값과 비교해 주세요. 현재 Blueprint와 다른 대상이면 구분하고, PIE 동작이나 Dispatcher 실행은 관찰한 것으로 단정하지 마세요. 현재 힌트 수준을 지켜 주세요."), false); break;
    case EQuick::ExplainBack: InsertDraft(LOCTEXT("ExplainPrompt", "제가 이해한 원인은: \n직접 관찰한 결과는: \n이 설명에서 확인할 점을 현재 힌트 수준으로 알려 주세요."), TEXT("입력창에 직접 이해한 내용을 적고 보내세요.")); break;
    case EQuick::Where: InsertDraft(LOCTEXT("WherePrompt", "찾고 싶은 메뉴 또는 노드: "), TEXT("입력창에 찾고 싶은 이름을 적고 보내세요.")); break;
    }
}
TSharedRef<SWidget> SKHUTutorPanel::BuildInputArea()
{
    return SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.InputBox"))).Padding(0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Notice.Info"))).Padding(10)
                .Visibility_Lambda([this] { return RecordDrafts.Contains(ActiveConversation) ? EVisibility::Visible : EVisibility::Collapsed; })
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))
                        .Text_Lambda([this] { const auto* D = RecordDrafts.Find(ActiveConversation); return FText::FromString(D ? FString::Printf(TEXT("학습 기록 참고 자료 · 대화 발췌 %d개\n%s"), D->Count, *D->Title) : FString()); })]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 0))
                        + SWrapBox::Slot()[StyledButton(LOCTEXT("ViewRecordReference", "참고 자료 보기"), TEXT("Tutor.GhostButton"), [this] { OpenPortfolio(ActiveConversation); }, [this] { return !Busy(); })]
                        + SWrapBox::Slot()[StyledButton(LOCTEXT("RemoveRecordReference", "첨부 해제"), TEXT("Tutor.GhostButton"), [this] { RecordDrafts.Remove(ActiveConversation); Status = TEXT("참고 자료 첨부를 해제했어요. 질문은 그대로 남아 있어요."); }, [this] { return !Busy(); })]]]]
            + SVerticalBox::Slot().AutoHeight()[SNew(SBox).MinDesiredHeight(54).MaxDesiredHeight(170)
                [SAssignNew(Input, SMultiLineEditableTextBox).Style(&Style->GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Tutor.InputText")))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11)).AutoWrapText(true)
                    .OnTextChanged_Lambda([this](const FText&) { if (bDraftConflict) { bDraftConflict = false; Status.Reset(); } })
                    .IsEnabled_Lambda([this] { return !bConversationReadOnly; })
                    .HintText_Lambda([this] { return bConversationReadOnly ? LOCTEXT("ChatInputReadOnly", "이 대화는 지금 이어갈 수 없어요")
                        : Busy() ? LOCTEXT("ChatInputBusy", "답변이 끝나면 이어서 물어볼 수 있어요") : LOCTEXT("ChatInput", "언리얼에서 무엇을 도와드릴까요? (Shift+Enter 줄바꿈)"); })
                    .ModiferKeyForNewLine(EModifierKey::Shift).OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type) { if (Type == ETextCommit::OnEnter && !Busy()) { Submit(Text.ToString()); } })]]
            + SVerticalBox::Slot().AutoHeight().Padding(6, 2, 6, 6)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SComboButton)
                    .ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.GhostButton"))).HasDownArrow(false).ContentPadding(FMargin(7, 5))
                    .ToolTipText(LOCTEXT("QuestionHelpersTip", "질문 도우미 · 바로 보내는 질문과 입력창 틀"))
                    .IsEnabled_Lambda([this] { return !bStopped; })
                    .OnGetMenuContent(FOnGetContent::CreateSP(this, &SKHUTutorPanel::HelperMenu))
                    .ButtonContent()[SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)[SNew(SBox).WidthOverride(13).HeightOverride(13)
                            [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Icon()))]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("QuestionHelpers", "도우미")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.ButtonText")))]]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SComboButton)
                    .ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.HintButton"))).HasDownArrow(false).ContentPadding(FMargin(9, 5))
                    .ToolTipText(LOCTEXT("HintTip", "도움 수준 · 다음 질문부터 적용돼요"))
                    .IsEnabled_Lambda([this] { return !bStopped && !Busy(); })
                    .OnGetMenuContent(FOnGetContent::CreateSP(this, &SKHUTutorPanel::HintMenu))
                    .ButtonContent()[SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.AccentLabel")))
                            .Text_Lambda([this] { return FText::Format(LOCTEXT("HintChip", "L{0} {1}"), FText::AsNumber(Hint), HintName(Hint)); })]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 0, 0)[SNew(SBox).WidthOverride(11).HeightOverride(11)
                            [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ChevronDown"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Accent()))]]]]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpacer)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SBox).Visibility_Lambda([this] { return Loop && Loop->CanResume() ? EVisibility::Visible : EVisibility::Collapsed; })
                    [StyledButton(LOCTEXT("Continue", "계속하기"), TEXT("Tutor.SecondaryButton"), [this] { if (Loop && Loop->CanResume()) { BeginActivity(); Loop->Resume(); } }, [this] { return Loop && Loop->CanResume(); })]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this] { return Busy() ? 1 : 0; })
                    + SWidgetSwitcher::Slot()[StyledButton(LOCTEXT("Send", "보내기"), TEXT("Tutor.PrimaryButton"), [this] { if (!Busy()) { Submit(Input->GetText().ToString()); } },
                        [this] { return !bConversationReadOnly && !Busy() && Input && !Input->GetText().ToString().TrimStartAndEnd().IsEmpty(); }, Style->GetBrush(TEXT("Icon.Send")))]
                    + SWidgetSwitcher::Slot()[SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.SecondaryButton"))).ContentPadding(FMargin(12, 7))
                        .ToolTipText(LOCTEXT("StopTip", "응답을 멈춰요. 지금까지 보인 내용은 남아요."))
                        .IsEnabled_Lambda([this] { return !bStopped && Busy(); }).OnClicked_Lambda([this] { CancelTask(); return FReply::Handled(); })
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SImage).Image(Style->GetBrush(TEXT("Icon.Stop"))).ColorAndOpacity(FSlateColor(Hex(TEXT("EF3535"))))]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("StopResponse", "중지")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.ButtonText")))]]]]]];
}
TSharedRef<SWidget> SKHUTutorPanel::BuildEmptyState()
{
    const auto TextStyle = [this](const TCHAR* Name) { return &Style->GetWidgetStyle<FTextBlockStyle>(Name); };
    const auto Starter = [this, TextStyle](const FSlateBrush* Icon, const FLinearColor& Tint, const FText& Title, const FText& Body, EQuick Kind) -> TSharedRef<SWidget>
    {
        return SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.StarterButton"))).ContentPadding(FMargin(12, 10))
            .IsEnabled_Lambda([this] { return !bStopped && !Busy() && !bConversationReadOnly; })
            .OnClicked_Lambda([this, Kind] { RunQuick(Kind); return FReply::Handled(); })
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[SNew(SBox).WidthOverride(16).HeightOverride(16)[SNew(SImage).Image(Icon).ColorAndOpacity(FSlateColor(Tint))]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)[SNew(STextBlock).Text(Title).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Label")))]
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Body).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Caption")))]];
    };
    const auto Pin = [this, TextStyle](const FText& Label) -> TSharedRef<SWidget>
    { return SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Pin"))).Padding(FMargin(8, 2))[SNew(STextBlock).Text(Label).TextStyle(TextStyle(TEXT("Tutor.Caption")))]; };
    auto Seen = SNew(SWrapBox).UseAllottedSize(true);
    for (const FText& Label : {LOCTEXT("SeeNodes", "선택한 노드·Graph 흐름"), LOCTEXT("SeeCompile", "Blueprint 컴파일 오류"), LOCTEXT("SeeCpp", "C++ 빌드 오류·소스"),
        LOCTEXT("SeeLog", "Output Log"), LOCTEXT("SeeActors", "선택 액터 관계·Details")})
    { Seen->AddSlot().Padding(0, 0, 5, 5)[Pin(Label)]; }
    return SNew(SBox).Visibility_Lambda([this] { return TurnCount == 0 && !bConversationReadOnly ? EVisibility::Visible : EVisibility::Collapsed; })
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[SNew(SBox).WidthOverride(36).HeightOverride(36)
                [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.HeroAvatar"))).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SImage).Image(Style->GetBrush(TEXT("Icon.SparkLarge"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Accent()))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)[SNew(STextBlock).Text(LOCTEXT("EmptyTitle", "무엇을 만들고 있나요?")).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Hero")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[SNew(STextBlock).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(TextStyle(TEXT("Tutor.Caption")))
                .Text(LOCTEXT("EmptyBody", "그래프와 오류를 직접 읽고, 정답 대신 다음 한 걸음을 알려 드려요. 에디터는 직접 조작하지 않아요."))]
            + SVerticalBox::Slot().AutoHeight()[SNew(SUniformGridPanel).SlotPadding(FMargin(0, 0, 8, 8))
                + SUniformGridPanel::Slot(0, 0)[Starter(FAppStyle::GetBrush(TEXT("Icons.Warning")), KHUTutorPalette::Warning(), LOCTEXT("StarterErrors", "최근 오류 설명"), LOCTEXT("StarterErrorsBody", "Compile 오류와 로그를 읽어요"), EQuick::Errors)]
                + SUniformGridPanel::Slot(1, 0)[Starter(Style->GetBrush(TEXT("Icon.Nodes")), KHUTutorPalette::Link(), bBlueprintPanel ? LOCTEXT("StarterNodes", "선택 노드의 역할") : LOCTEXT("StarterTarget", "선택 대상의 역할"), LOCTEXT("StarterNodesBody", "실행·값 흐름을 설명해요"), EQuick::Selection)]
                + SUniformGridPanel::Slot(0, 1)[Starter(Style->GetBrush(TEXT("Icon.Steps")), KHUTutorPalette::Accent(), LOCTEXT("StarterPractice", "한 단계씩 실습"), LOCTEXT("StarterPracticeBody", "직접 해 볼 행동을 하나씩 받아요"), EQuick::Practice)]
                + SUniformGridPanel::Slot(1, 1)[Starter(FAppStyle::GetBrush(TEXT("Icons.Search")), KHUTutorPalette::Icon(), LOCTEXT("StarterWhere", "이거 어디 있어?"), LOCTEXT("StarterWhereBody", "메뉴·노드 위치를 찾아요"), EQuick::Where)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 8, 6)[SNew(SComboButton)
                .ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.SecondaryButton")))
                .IsEnabled_Lambda([this] { return !bStopped && !Busy() && !bConversationReadOnly; })
                .OnGetMenuContent(FOnGetContent::CreateSP(this, &SKHUTutorPanel::PracticeMenu))
                .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("CppStart", "C++/CS 실습 고르기"))
                    .TextStyle(TextStyle(TEXT("Tutor.ButtonText")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 8, 0)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Evidence"))).Padding(FMargin(12, 10))
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(STextBlock).Text(LOCTEXT("SeeTitle", "튜터가 볼 수 있는 것")).TextStyle(TextStyle(TEXT("Tutor.Label")))]
                    + SVerticalBox::Slot().AutoHeight()[Seen]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Caption")))
                        .Text(LOCTEXT("SeeLimits", "볼 수 없는 것: 화면 스크린샷, 실행(PIE) 중 동작, 다른 프로젝트"))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 8, 0)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Coach"))).Padding(FMargin(10, 8))
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 1, 8, 0)[SNew(SBox).WidthOverride(14).HeightOverride(14)
                        [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Info"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Accent()))]]
                    + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Caption")))
                        .Text_Lambda([this] { return FText::Format(LOCTEXT("EmptyHint", "지금 도움 수준은 L{0} {1}예요. 입력창 아래에서 언제든 바꿀 수 있어요."), FText::AsNumber(Hint), HintName(Hint)); })]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SNew(STextBlock).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Micro")))
                .Text(LOCTEXT("EmptyRecords", "이전 대화는 기록(시계 아이콘)에 보관돼요."))]];
}
FText SKHUTutorPanel::MetaLine() const
{
    FNumberFormattingOptions One; One.MaximumFractionalDigits = 1;
    TArray<FString> Parts;
    Parts.Add(bMock ? TEXT("개발 Mock 테스트") : bDiagnosing ? TEXT("연결 확인 중") : Loop && Loop->State() == EKHUAgentState::AskContinue ? TEXT("일시정지 · 선택 대기") : Busy() ? TEXT("응답 생성 중") : bApiConnected ? TEXT("연결됨 · ") + ModelLabel() : TEXT("연결 필요"));
    if (TaskCredits.IsSet()) { Parts.Add(FText::Format(LOCTEXT("MetaTask", "이번 작업 ≈ {0} 크레딧"), FText::AsNumber(TaskCredits.GetValue(), &One)).ToString()); }
    if (Balance.IsSet()) { Parts.Add(FText::Format(LOCTEXT("MetaBalance", "잔액 {0} (연결 확인 시점)"), FText::AsNumber(Balance.GetValue(), &One)).ToString()); }
    return FText::FromString(FString::Join(Parts, TEXT(" · ")));
}
FSlateColor SKHUTutorPanel::StatusColor() const
{
    if (bMock) { return FSlateColor(KHUTutorPalette::Link()); }
    if (bDiagnosing || Busy()) { return FSlateColor(KHUTutorPalette::Warning()); }
    return FSlateColor(bApiConnected ? KHUTutorPalette::Success() : KHUTutorPalette::Error());
}
FString SKHUTutorPanel::ModelLabel() const
{
    const auto* Tier = Config.Tiers.Find(TEXT("T1")); const FString Model = Tier && !Tier->Chain.IsEmpty() ? Tier->Chain[0] : FString();
    if (Model.Contains(TEXT("sonnet"))) { return TEXT("Claude Sonnet"); }
    if (Model.Contains(TEXT("haiku"))) { return TEXT("Claude Haiku"); }
    if (Model.Contains(TEXT("opus"))) { return TEXT("Claude Opus"); }
    return Model.IsEmpty() ? FString(TEXT("모델 미확인")) : Model;
}

void SKHUTutorPanel::Construct(const FArguments& Args)
{
    BlueprintEditor = Args._BlueprintEditor;
    if (auto Editor = BlueprintEditor.Pin()) { bBlueprintPanel = true; TargetBlueprint = Editor->GetBlueprintObj(); }
    Style = FKHUTutorStyle::Get(); Hint = FMath::Clamp(GetDefault<UKHUTutorSettings>()->DefaultHintLevel, 0, 3);
    Config = IKHUTutorModule::Get().PanelConfig(); Tools = IKHUTutorModule::Get().CreatePanelTools(); DiagnosticTransport = KHUCreateHttpTransport();
    ProjectMemory = MakeShared<FKHUProjectMemory>(FPaths::ProjectSavedDir() / TEXT("KHUTutor/ProjectMemory.json"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), Config.Context);
    Feedback = MakeShared<FKHUFeedbackStore>(FPaths::ProjectSavedDir() / TEXT("KHUTutor/Feedback.json"));
    FString ExistingKey, KeyIssue; const bool HasKey = Secret().Load(ExistingKey, KeyIssue); ExistingKey.Reset();
    ConnectionMessage = HasKey ? LOCTEXT("SettingsKeyExists", "저장된 키가 있어요. 연결 확인을 눌러 주세요.").ToString() : LOCTEXT("SettingsNeedsKey", "API 키를 입력하고 연결해 주세요.").ToString();
    Status.Reset(); ToolStatus = KeyStatus = LOCTEXT("Unchecked", "미확인").ToString();
    auto SettingsPage = BuildSettingsPage();
    auto MemoryPage = BuildProjectsPage();
    auto ChatPage = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[BuildContextBar()]
        + SVerticalBox::Slot().FillHeight(1)[SAssignNew(Conversation, SScrollBox)]
        + SVerticalBox::Slot().AutoHeight().Padding(14, 4)[SNew(SBox).Visibility_Lambda([this] { return !Loop && StoredConversation.Turns.Num() > 40 ? EVisibility::Visible : EVisibility::Collapsed; })
            [SNew(SWrapBox).UseAllottedSize(true)
                + SWrapBox::Slot().Padding(0, 0, 6, 0)[Button(LOCTEXT("EarlierHistory", "이전 40개"), [this] { RenderStoredConversation(FMath::Max(0, HistoryStart - 40)); }, [this] { return HistoryStart > 0; })]
                + SWrapBox::Slot().Padding(0, 0, 6, 0)[Button(LOCTEXT("LaterHistory", "다음 40개"), [this] { RenderStoredConversation(FMath::Min(StoredConversation.Turns.Num() - 1, HistoryStart + 40)); }, [this] { return HistoryStart + 40 < StoredConversation.Turns.Num(); })]
                + SWrapBox::Slot()[Button(LOCTEXT("LatestHistory", "최근 대화"), [this] { RenderStoredConversation(FMath::Max(0, StoredConversation.Turns.Num() - 40)); })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(14, 4, 14, 2)[SNew(SBox).Visibility_Lambda([this] { return bHasTask && !Busy() && !bConversationReadOnly && TurnCount > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
            [SNew(SWrapBox).UseAllottedSize(true)
                + SWrapBox::Slot().Padding(0, 0, 6, 6)[StyledButton(LOCTEXT("ChipRecheck", "수정했어요, 재확인"), TEXT("Tutor.ChipButton"), [this] { RunQuick(EQuick::Recheck); }, [this] { return !Busy(); }, nullptr,
                    LOCTEXT("ChipRecheckTip", "새 그래프와 최신 Compile 기록을 읽어 이전 근거와 비교해요."))]
                + SWrapBox::Slot().Padding(0, 0, 6, 6)[StyledButton(LOCTEXT("ChipExplain", "이해한 내용 설명하기"), TEXT("Tutor.ChipButton"), [this] { RunQuick(EQuick::ExplainBack); }, [this] { return !Busy(); }, nullptr,
                    LOCTEXT("ChipExplainTip", "입력창에 내 설명 틀을 넣어요. 채운 뒤 보내요."))]
                + SWrapBox::Slot().Padding(0, 0, 6, 6)[StyledButton(LOCTEXT("Solved", "해결됐어요"), TEXT("Tutor.ChipButton"), [this] { ResetTask(true); }, [this] { return !Busy(); }, nullptr,
                    LOCTEXT("SolvedTip", "직접 해결했다고 표시해요. 다음 질문은 새 작업으로 시작해요."))]]]
        + SVerticalBox::Slot().AutoHeight().Padding(14, 4)[SNew(SBorder).Visibility_Lambda([this] { return !bApiConnected && !bMock ? EVisibility::Visible : EVisibility::Collapsed; })
            .BorderImage(Style->GetBrush(TEXT("Tutor.Notice.Error"))).Padding(FMargin(10, 8))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(14).HeightOverride(14)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Error"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Error()))]]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.ButtonText")))
                    .Text_Lambda([this] { return bDiagnosing ? LOCTEXT("ConnectChecking", "연결을 확인하고 있어요.") : LOCTEXT("ConnectNeeded", "ChatKHU 연결이 필요해요. 작성한 질문은 그대로 남아 있어요."); })]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[StyledButton(LOCTEXT("ConnectCta", "설정에서 연결"), TEXT("Tutor.PrimaryButton"), [this] { ShowPage(2); })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(14, 6)[SAssignNew(ComposerNotice, SBorder)
            .Visibility_Lambda([this] { return Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            .BorderImage_Lambda([this] { return Style->GetBrush(bDraftConflict ? TEXT("Tutor.Notice.Warning") : TEXT("Tutor.Notice.Info")); }).Padding(FMargin(12, 10))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 8, 0)[SNew(SBox).WidthOverride(16).HeightOverride(16)
                    [SNew(SImage).Image_Lambda([this] { return FAppStyle::GetBrush(bDraftConflict ? TEXT("Icons.Warning") : TEXT("Icons.Info")); })
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(bDraftConflict ? KHUTutorPalette::Warning() : KHUTutorPalette::Link()); })]]
                + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).AutoWrapText(true).LineHeightPercentage(1.3f)
                    .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body"))).Text_Lambda([this] { return FText::FromString(Status); })]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8, 0, 0, 0)
                    [StyledButton(LOCTEXT("DismissComposerNotice", "닫기"), TEXT("Tutor.GhostButton"), [this] { Status.Reset(); bDraftConflict = false; })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(14, 4, 14, 4)[BuildInputArea()]
        + SVerticalBox::Slot().AutoHeight().Padding(16, 0, 16, 8)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Dot"))).ColorAndOpacity(this, &SKHUTutorPanel::StatusColor)]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SKHUTutorPanel::MetaLine).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Micro")))]];
    ChildSlot[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Panel"))).Padding(0)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[BuildHeader()]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 4, 12, 8)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 4))
            + SWrapBox::Slot()[SNew(SCheckBox).Style(&Style->GetWidgetStyle<FCheckBoxStyle>(TEXT("Settings.Choice")))
                .IsChecked_Lambda([this] { return ActivePage == 0 || ActivePage == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { ShowPage(0); })
                [SNew(STextBlock).Text(LOCTEXT("MainChat", "채팅")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]]
            + SWrapBox::Slot()[SNew(SCheckBox).Style(&Style->GetWidgetStyle<FCheckBoxStyle>(TEXT("Settings.Choice")))
                .IsChecked_Lambda([this] { return ActivePage == 3 || ActivePage == 4 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { OpenRecords(); })
                [SNew(STextBlock).Text(LOCTEXT("MainRecords", "학습 기록")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))]]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Divider")))]]
        + SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(232)
                .Visibility_Lambda([this] { return ActivePage != 3 && ActivePage != 4 && bSessionSidebarVisible && GetCachedGeometry().GetLocalSize().X >= 720.0f ? EVisibility::Visible : EVisibility::Collapsed; })
                [BuildSessionSidebar()]]
            + SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Pages, SWidgetSwitcher)
            + SWidgetSwitcher::Slot()[ChatPage]
            + SWidgetSwitcher::Slot()[MemoryPage]
            + SWidgetSwitcher::Slot()[SettingsPage]
            + SWidgetSwitcher::Slot()[BuildPortfolioPage()]
            + SWidgetSwitcher::Slot()[BuildRecordsPage()]]]]];
    ClearConversation();
    RefreshProjects();
}
void SKHUTutorPanel::ShowPage(int32 Page)
{
    if (bStopped || !Pages) { return; }
    ActivePage = FMath::Clamp(Page, 0, 4); Pages->SetActiveWidgetIndex(ActivePage);
    if (ActivePage == 1) { RefreshProjects(); }
}
void SKHUTutorPanel::CancelTask()
{
    if (Loop) { bReleasingLoop = true; auto Cancelled = MoveTemp(Loop); bHasTask = false; Cancelled->Cancel(); bReleasingLoop = false; }
    if (Activity) { Activity->bLive = false; }
    Links.Reset(); CurrentLinkIds.Reset(); Observation.Reset(); TaskGraph.Reset();
    Status = LOCTEXT("StoppedResponse", "응답을 중지했어요. 지금까지 내용은 남아 있고, 새 질문을 보낼 수 있어요.").ToString();
}
FText SKHUTutorPanel::CompactContextLabel() const
{
    if (bMock) { return LOCTEXT("MockBadge", "개발 Mock 테스트"); }
    if (!bBlueprintPanel) { return LOCTEXT("ProjectBadge", "프로젝트 · 일반 질문"); }
    return FText::FromString((TargetBlueprint.IsValid() ? TargetBlueprint->GetName() : TEXT("대상 없음"))
        + (TaskGraph.IsValid() ? TEXT(" · ") + TaskGraph->GetName() : FString())
        + TEXT(" · ") + (bSelectionOnly ? TEXT("선택 노드") : TEXT("현재 Graph")));
}
SKHUTutorPanel::~SKHUTutorPanel() { Stop(); }
void SKHUTutorPanel::Stop()
{
    if (bStopped) { return; } bStopped = true; ++DiagnosticGeneration;
    if (CancelDiagnostic) { auto Cancel = MoveTemp(CancelDiagnostic); Cancel(); }
    if (Loop) { Loop->Cancel(); Loop.Reset(); }
    if (Tools) { Tools->Cancel(); Tools.Reset(); }
    Links.Reset(); CurrentLinkIds.Reset(); Observation.Reset(); TaskGraph.Reset();
    if (KeyInput) { KeyInput->SetText(FText::GetEmpty()); }
}
bool SKHUTutorPanel::Busy() const { return Loop && Loop->State() != EKHUAgentState::Idle; }
FText SKHUTutorPanel::ProjectProgressLabel() const
{
    return FText::FromString(ProjectMemory ? ProjectMemory->Describe(TargetBlueprint.IsValid() ? TargetBlueprint->GetPathName() : TEXT(""), Hint) : TEXT("프로젝트 기억을 사용할 수 없습니다."));
}
void SKHUTutorPanel::RememberProgress()
{
    if (!ProjectMemory || !MemoryInput || Busy() || bMock) { return; }
    const bool Ok = ProjectMemory->Remember(TargetBlueprint.IsValid() ? TargetBlueprint->GetPathName() : TEXT(""), MemoryKind, MemoryInput->GetText().ToString(), Hint);
    if (Ok) { MemoryInput->SetText(FText::GetEmpty()); }
    AddNotice(Ok ? ENotice::Success : ENotice::Error, Ok ? LOCTEXT("Remembered", "중요한 내용을 저장했어요. 다음 질문과 새 대화에서 이어받아요. 같은 종류의 메모는 가장 최근 내용이 우선돼요.").ToString() : ProjectMemory->Error());
}
FText SKHUTutorPanel::Header() const
{
    const auto* Tier = Config.Tiers.Find(TEXT("T1")); const FString Model = Tier && !Tier->Chain.IsEmpty() ? Tier->Chain[0] : TEXT("—");
    return FText::Format(LOCTEXT("Header", "에디터: {0}  ·  ChatKHU: {1}\nClaude · {2}  |  잔액 {3}  ·  이번 작업 ≈{4}\n잔액은 마지막 연결 확인 시점의 값입니다."),
        FText::FromString(ToolStatus), FText::FromString(KeyStatus), FText::FromString(bMock ? TEXT("Mock") : Model),
        Balance.IsSet() ? FText::AsNumber(Balance.GetValue()) : LOCTEXT("Unknown", "미확인"),
        TaskCredits.IsSet() ? FText::AsNumber(TaskCredits.GetValue()) : LOCTEXT("Unknown", "미확인"));
}
FText SKHUTutorPanel::ContextLabel() const
{
    if (bMock) { return LOCTEXT("MockContext", "개발 Mock · 실제 Blueprint 문맥을 전송하지 않습니다."); }
    if (!bBlueprintPanel) { return LOCTEXT("GeneralContext", "일반 질문 · Blueprint의 Window > KHU Tutor에서 대상을 고정할 수 있어요."); }
    const FString Asset = TargetBlueprint.IsValid() ? TargetBlueprint->GetPathName() : TEXT("대상 없음");
    const FString Scope = bSelectionOnly ? TEXT("선택 노드") : TEXT("현재 Graph 요약 · 최대 60노드/출력 제한");
    const FString State = Observation ? (Observation->IsCurrent() ? TEXT("읽음 ") + Observation->ReadAt : TEXT("그래프 변경됨 · 다시 읽기 필요")) : TEXT("전송할 때 읽습니다");
    return FText::FromString(Asset + TEXT("\nEditor 자산 · ") + Scope + TEXT(" · ") + (TaskGraph.IsValid() ? TaskGraph->GetName() : TEXT("현재 Graph")) + TEXT("\n") + State);
}
bool SKHUTutorPanel::CaptureContext()
{
    if (bMock || !bBlueprintPanel) { Observation.Reset(); CurrentLinkIds.Reset(); return true; }
    auto Editor = BlueprintEditor.Pin(); auto* BP = TargetBlueprint.Get();
    if (!Editor || !BP || Editor->GetBlueprintObj() != BP)
    { Status = TEXT("대상 Blueprint가 닫혔거나 변경됐어요. 해당 Blueprint에서 패널을 다시 열어 주세요."); return false; }
    auto* Graph = Editor->GetFocusedGraph();
    if (bHasTask && (!TaskGraph.IsValid() || TaskGraph.Get() != Graph))
    { Status = TEXT("다른 Graph로 전환했어요. 도우미 메뉴의 ‘새 작업으로 문맥 나누기’로 문맥을 분리해 주세요."); return false; }
    auto Fresh = MakeShared<FKHUEditorObservation>(BP, Graph, bSelectionOnly, FKHUGraphDigest::SelectedNodes(BP, Graph));
    if (!Fresh->Error.IsEmpty()) { Status = Fresh->Error; return false; }
    Observation = Fresh; TaskGraph = Graph;
    CaptureLinks(Fresh->AssetPath, Fresh->Digest, Fresh);
    return true;
}
void SKHUTutorPanel::CaptureLinks(const FString& Asset, const FString& Digest, TSharedPtr<FKHUEditorObservation> Evidence)
{
    CurrentLinkIds.Reset(); if (!Evidence || !Evidence->IsCurrent()) { return; }
    FString Issue; auto* BP = FKHUGraphDigest::FindLoadedBlueprint(Asset, Issue); if (!BP) { return; }
    TArray<FString> Lines; Digest.ParseIntoArrayLines(Lines);
    for (const auto& Line : Lines)
    {
        int32 Space; if (!Line.StartsWith(TEXT("#")) || !Line.FindChar(' ', Space)) { continue; }
        const FString Ref = Line.Left(Space);
        auto* Node = FKHUNodeRefCache::Get().Resolve(BP, Evidence->GraphName + TEXT(":") + Ref, Issue); if (!Node) { continue; }
        FLink Link; Link.Native = TEXT("KHUTutorTools.KHUTutorShowToolset.FocusGraphNode"); Link.Args = MakeShared<FJsonObject>(); Link.Observation = Evidence;
        Link.Args->SetStringField(TEXT("assetPath"), Asset); Link.Args->SetStringField(TEXT("nodeRef"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        CurrentLinkIds.Add(Ref, FString::FromInt(Links.Add(MoveTemp(Link))));
    }
}
void SKHUTutorPanel::Submit(const FString& Raw, bool bAttachGraph)
{
    const FString Question = Raw.TrimStartAndEnd(); if (bStopped || Busy() || Question.IsEmpty()) { return; }
    if (bConversationReadOnly) { Status = TEXT("이 대화는 현재 창에서 이어갈 수 없어요. 보관 상태와 연결된 Blueprint를 확인해 주세요."); return; }
    if (!bMock && (!bApiConnected || bDiagnosing)) { Status = TEXT("설정에서 API 키를 등록하고 ‘연결 확인’을 완료해 주세요."); ShowPage(2); return; }
    if (Question.Len() > 16000) { Status = LOCTEXT("Long", "질문을 16,000자 이내로 나누어 주세요.").ToString(); return; }
    FString RecordContext;
    if (const auto* Draft = RecordDrafts.Find(ActiveConversation))
    {
        if (Question != Input->GetText().ToString().TrimStartAndEnd())
        { bDraftConflict = true; Status = TEXT("학습 기록 질문을 작성 중이에요. 먼저 보내거나 참고 자료 첨부를 해제해 주세요."); return; }
        FKHUSavedConversation Saved;
        if (Draft->Hint != Hint || !ProjectMemory || !ProjectMemory->ReadConversation(ActiveConversation, Hint, Saved)
            || Saved.bArchived || Saved.Scope != AssetScope() || Saved.bPortfolioHidden || !Saved.Portfolio
            || FKHUPortfolio::Read(Saved.Portfolio).Revision != Draft->Revision
            || (PortfolioConversation.Id == ActiveConversation && bPortfolioDirty))
        { bDraftConflict = true; Status = TEXT("학습 기록이나 도움 수준이 바뀌었어요. 참고 자료를 열어 저장한 뒤 ‘설명 다듬기’를 다시 눌러 주세요. 질문은 보존했어요."); return; }
        RecordContext = Draft->Context;
    }
    if (bAttachGraph && RecordContext.IsEmpty()) { if (!CaptureContext()) { return; } }
    else { Observation.Reset(); CurrentLinkIds.Reset(); }
    if (!Loop)
    {
        if (!bMock && Config.Context.bProjectMemory && ActiveConversation.IsEmpty())
        { if (!ProjectMemory || !ProjectMemory->CreateConversation(AssetScope(), ActiveFolder, Hint, ActiveConversation))
          { Status = ProjectMemory ? ProjectMemory->Error() : TEXT("대화를 저장할 수 없습니다."); return; } }
        if (!bMock && !ActiveConversation.IsEmpty())
        { FKHUSavedConversation Saved; if (!ProjectMemory || !ProjectMemory->ReadConversation(ActiveConversation, Hint, Saved) || Saved.bArchived || Saved.Scope != AssetScope())
          { Status = TEXT("선택한 대화를 이어갈 수 없어요. 기록에서 다시 열어 주세요."); return; } }
        FString Error; Loop = IKHUTutorModule::Get().CreatePanelLoop(bMock, Error); Config = IKHUTutorModule::Get().PanelConfig();
        if (!Loop)
        { AddNotice(ENotice::Error, Error, {{LOCTEXT("OpenSettings", "설정 열기"), [this] { ShowPage(2); }, {}}}); return; }
        Loop->SetProjectMemory(bMock ? nullptr : ProjectMemory, TargetBlueprint.IsValid() ? TargetBlueprint->GetPathName() : TEXT(""));
        ReceiptCount = 0; UsageSeen = 0; bHasTask = false; SeenSession = 1;
    }
    Loop->SetEditorObservation(Observation && !bMock ? Observation->Json() : FString());
    AddMessage(LOCTEXT("Student", "나"), Question); Stuck.Note(Question);
    // The card exists before the request starts; a synchronous reply then lands after it.
    BeginActivity(); Status.Reset();
    const TWeakPtr<SKHUTutorPanel> Weak = SharedThis(this);
    const FString SubmittedConversation = ActiveConversation;
    const int32 BeforeRequests = bHasTask ? static_cast<int32>(Loop->Receipt()->GetNumberField(TEXT("iterations"))) : 0;
    const bool Started = bHasTask ? Loop->FollowUp(Question, Hint, RecordContext) : Loop->Ask(Question, TEXT("T1"), Hint,
        [Weak](EKHUAgentState State, const FString& Text) { if (auto P = Weak.Pin(); P && !P->bStopped) { P->OnEvent(State, Text); } }, false, true, bMock ? FString() : ActiveConversation, RecordContext);
    bHasTask |= Started;
    const bool RecordNotSent = Started && !RecordContext.IsEmpty() && Loop->State() == EKHUAgentState::Idle
        && static_cast<int32>(Loop->Receipt()->GetNumberField(TEXT("iterations"))) == BeforeRequests;
    if (Started && !RecordNotSent) { Input->SetText(FText::GetEmpty()); Drafts.Remove(SubmittedConversation); RecordDrafts.Remove(SubmittedConversation); }
    else if (RecordNotSent)
    { bDraftConflict = true; Status = TEXT("요청을 보내지 못해 질문과 참고 자료를 보존했어요. 위 안내를 확인해 주세요. 자료가 너무 길면 참고할 대화를 줄이거나 내 설명을 간결하게 다듬은 뒤 다시 준비하세요."); }
    if (!Started)
    {
        if (Activity) { Activity->bLive = false; }
        AddNotice(ENotice::Warning, LOCTEXT("CannotStart", "작업을 이어갈 수 없어요. 새 대화로 시작해 주세요.").ToString(),
            {{LOCTEXT("NewConversationTip", "새 대화"), [this] { NewConversation(ActiveFolder); }, [this] { return !Busy(); }}});
    }
}
void SKHUTutorPanel::ChangeHint(int32 Level, bool bRequest)
{
    const int32 Previous = Hint; Hint = FMath::Clamp(Level, 0, 3);
    if (Hint < Previous && !ActiveConversation.IsEmpty() && ProjectMemory)
    {
        // Remove old rich text, copy controls and tool receipts before displaying a lower hint.
        FKHUSavedConversation Saved;
        if (ProjectMemory->ReadConversation(ActiveConversation, Hint, Saved))
        { StoredConversation = MoveTemp(Saved); RenderStoredConversation(FMath::Max(0, StoredConversation.Turns.Num() - 40)); }
        else { ClearConversation(); AddNotice(ENotice::Error, ProjectMemory->Error()); }
    }
    if (bRequest && bHasTask) { Submit(Hint == 0 ? TEXT("정답을 말하지 말고 질문으로 이끌어 주세요.") : TEXT("같은 문제를 현재 힌트 레벨에 맞춰 더 설명해 주세요.")); }
}
void SKHUTutorPanel::ResetTask(bool bSolved)
{
    const FString SolvedConversation = ActiveConversation;
    if (bSolved && Loop) { Loop->MarkSolved(); }
    if (Loop) { bReleasingLoop = true; auto Cancelled = MoveTemp(Loop); bHasTask = false; Cancelled->Cancel(); bReleasingLoop = false; } bHasTask = false; ReceiptCount = 0; UsageSeen = 0; TaskCredits.Reset();
    Links.Reset(); CurrentLinkIds.Reset(); Observation.Reset(); TaskGraph.Reset(); ClearConversation();
    ActiveConversation.Reset(); ActiveConversationTitle.Reset(); StoredConversation = FKHUSavedConversation(); bConversationReadOnly = false; ShownReview.Reset();
    Status.Reset(); Stuck.Reset();
    if (bSolved)
    {
        TArray<FNoticeAction> Actions;
        if (!SolvedConversation.IsEmpty() && !bMock)
        { Actions.Add({LOCTEXT("RecordSolved", "해결 과정 기록하기"), [this, SolvedConversation] { OpenPortfolio(SolvedConversation); }, [this] { return !Busy(); }}); }
        AddNotice(ENotice::Success, LOCTEXT("SolvedNote", "해결했어요! 방금 채팅은 목록에 남아 있어요. 해결 과정을 학습 기록으로 정리하거나 새 질문을 시작하세요.").ToString(), MoveTemp(Actions));
        RefreshProjects();
    }
}
void SKHUTutorPanel::OnEvent(EKHUAgentState State, const FString& Text)
{
    if (bReleasingLoop) { return; }
    AddReceipts();
    if (Loop)
    {
        if (!bMock) { ActiveConversation = Loop->Receipt()->GetStringField(TEXT("task_id")); }
        const auto R = Loop->Receipt(); const int32 Session = static_cast<int32>(R->GetNumberField(TEXT("conversation_segment")));
        if (Session > SeenSession) { SeenSession = Session; AddNotice(ENotice::Divider, LOCTEXT("Rotated", "대화가 길어져 핵심 진행을 이어받은 새 구간이에요. 현재 에디터 상태는 다시 확인해요.").ToString()); }
        if (State == EKHUAgentState::Idle && R->GetBoolField(TEXT("project_memory_save_failed")))
        { AddNotice(ENotice::Error, LOCTEXT("MemoryFailed", "일부 대화를 저장하지 못해 자동 대화 전환을 멈췄어요. 이 답변은 화면에 남지만 다음 대화에 모두 이어진다고 보장할 수 없어요.\n").ToString() + R->GetStringField(TEXT("project_memory_error"))); }
    }
    const bool Done = State == EKHUAgentState::Idle || State == EKHUAgentState::AskContinue;
    if (!Done)
    {
        // Progress belongs in the turn card; the status line keeps only actionable messages.
        if (Activity && Activity->bLive) { Activity->Step = Text.Left(120); Status.Reset(); } else { Status = Text.Left(100); }
    }
    else { Status.Reset(); if (Activity) { Activity->bLive = false; } }
    if (State == EKHUAgentState::Idle)
    {
        if (Loop)
        {
            // Keep the receipt alive: GetArrayField returns a reference into it.
            const auto UsageReceipt = Loop->Receipt();
            const auto& Usage = UsageReceipt->GetArrayField(TEXT("usage")); double Sum = 0; bool Known = Usage.Num() > UsageSeen;
            for (int32 I = UsageSeen; I < Usage.Num(); ++I) { double Credits = 0; if (Usage[I]->AsObject()->TryGetNumberField(TEXT("credits_estimate"), Credits)) { Sum += Credits; } else { Known = false; } }
            UsageSeen = Usage.Num(); if (Known) { PendingAnswerCredits = Sum; }
        }
        AddMessage(bMock ? LOCTEXT("MockTutor", "튜터 · Mock 테스트 응답") : LOCTEXT("Tutor", "KHU Tutor"), Text, true, Hint);
        // L0 answers stay a single question, so a stuck beginner gets the choice here; the level changes only if they click.
        if (Stuck.TakeOffer(Hint))
        {
            AddNotice(ENotice::Info, FText::Format(LOCTEXT("StuckOffer", "막혀 있다면 도움 수준을 한 단계 올려 볼 수 있어요. 지금은 ‘{0}’ 단계라 튜터가 질문으로만 도와요. ‘{1}’로 올리면 어디를 보면 좋을지 알려 줘요. 그대로 두고 계속 생각해도 괜찮아요."), HintName(0), HintName(1)).ToString(),
                {{FText::Format(LOCTEXT("StuckRaise", "‘{0}’로 올리기"), HintName(1)), [this] { if (Hint == 0) { ChangeHint(1, true); } }, [this] { return Hint == 0 && !Busy(); }}});
        }
    }
    else if (State == EKHUAgentState::AskContinue)
    {
        // A paused task remains Busy to block unrelated submissions. Its notice has separate controls.
        const TWeakPtr<FKHUAgentLoop> PausedLoop = Loop;
        const auto CanContinue = [this, PausedLoop] { return Loop && Loop == PausedLoop.Pin() && Loop->CanResume(); };
        const auto CanStop = [this, PausedLoop] { return Loop && Loop == PausedLoop.Pin() && Loop->State() == EKHUAgentState::AskContinue; };
        AddNotice(ENotice::Warning, Text, {
            {LOCTEXT("Continue", "계속하기"), [this, CanContinue] { if (CanContinue()) { BeginActivity(); Loop->Resume(); } }, CanContinue},
            {LOCTEXT("StopHere", "여기서 멈추기"), [this, CanStop] { if (CanStop()) { CancelTask(); } }, CanStop}});
    }
    // Errors, cancels and paused replies also end in Idle; only a review produced by this answer gets a card.
    if (State == EKHUAgentState::Idle && Loop && !Loop->LearningCoach().Id().IsEmpty() && Loop->LearningCoach().Id() != ShownReview)
    { ShownReview = Loop->LearningCoach().Id(); AddCoachCard(Loop->LearningCoach()); }
    if (Done) { RefreshProjects(); }
}

// ---------------------------------------------------------------- conversation items
void SKHUTutorPanel::ClearConversation()
{
    if (!Conversation) { return; }
    Conversation->ClearChildren(); MessageCount = 0; TurnCount = 0; Activity.Reset();
    Conversation->AddSlot().Padding(16, 18, 8, 8)[BuildEmptyState()];
}
void SKHUTutorPanel::AddMessage(const FText& Who, const FString& RawText, bool bLinks, int32 TurnHint)
{
    if (!Conversation || bStopped) { return; }
    const FString Text = Who.EqualTo(LOCTEXT("Student", "나")) ? FKHUPortfolio::DisplayQuestion(RawText) : RawText;
    if (Who.EqualTo(LOCTEXT("Notice", "안내"))) { AddNotice(ENotice::Info, Text); return; }
    if (++MessageCount > 80) { ClearConversation(); Links.Reset(); CurrentLinkIds.Reset(); MessageCount = 1; }
    ++TurnCount;
    const TMap<FString, FString> Empty; const TWeakPtr<SKHUTutorPanel> Weak = SharedThis(this);
    const bool Student = Who.EqualTo(LOCTEXT("Student", "나"));
    TSharedRef<SWidget> Body = SNew(SKHUSelectableText)
        .Text(Text).LinkIds(bLinks && Hint > 0 ? CurrentLinkIds : Empty).Style(Style)
        .OnNavigate(FSlateHyperlinkRun::FOnClick::CreateLambda([Weak](const FSlateHyperlinkRun::FMetadata& Data)
        { if (auto P = Weak.Pin()) { if (const auto* Id = Data.Find(TEXT("target"))) { P->Navigate(*Id); } } }));
    const auto Copy = IconButton(Style->GetBrush(TEXT("Icon.Copy")), LOCTEXT("CopyMessage", "복사"), [Text] { FPlatformApplicationMisc::ClipboardCopy(*Text); });
    if (Student)
    {
        Conversation->AddSlot().Padding(14, 7)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Card"))).Padding(FMargin(12, 8, 6, 10))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(22).HeightOverride(22)
                        [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.StudentAvatar"))).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                            [SNew(STextBlock).Text(LOCTEXT("Student", "나")).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Label")))]]]
                    + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpacer)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Copy]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 6, 0)[Body]]];
    }
    else
    {
        const FString ConversationId = ActiveConversation; const FString AnswerId = FKHUFeedbackStore::AnswerId(ActiveConversation, Text);
        const int32 AnswerHint = TurnHint >= 0 ? TurnHint : Hint;
        const TOptional<double> Credits = PendingAnswerCredits; PendingAnswerCredits.Reset();
        const auto CanReask = [this] { return !Busy() && bHasTask && Loop.IsValid() && !bConversationReadOnly; };
        const auto Rate = [this, AnswerId, ConversationId, AnswerHint](const TCHAR* Rating)
        {
            if (!Feedback || bMock) { return; }
            FString Result;
            if (!Feedback->Toggle(AnswerId, Rating, ConversationId, AnswerHint, Result)) { Status = Feedback->Error(); return; }
            Status = Result.IsEmpty() ? TEXT("피드백 선택을 지웠어요.") : TEXT("피드백을 이 PC에만 기록했어요. 모델에 보내거나 점수로 쓰지 않아요.");
        };
        const auto RatingTint = [this, AnswerId](const TCHAR* Rating) { return TFunction<FSlateColor()>([this, AnswerId, Rating]
            { return FSlateColor(Feedback && Feedback->Get(AnswerId) == Rating ? KHUTutorPalette::Accent() : KHUTutorPalette::Icon()); }); };
        auto HeaderRow = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(22).HeightOverride(22)
                [SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Avatar"))).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SImage).Image(Style->GetBrush(TEXT("Icon.Spark"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Accent()))]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 6, 0)[SNew(STextBlock).Text(Who).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Strong")))];
        if (TurnHint >= 0)
        {
            HeaderRow->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Badge"))).Padding(FMargin(7, 1))
                [SNew(STextBlock).Text(FText::Format(LOCTEXT("AnswerHint", "L{0} {1}"), FText::AsNumber(TurnHint), HintName(TurnHint))).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Badge")))]];
        }
        FNumberFormattingOptions One; One.MaximumFractionalDigits = 1;
        const FText CreditText = Credits.IsSet() ? FText::Format(LOCTEXT("AnswerCredits", "≈ {0} 크레딧"), FText::AsNumber(Credits.GetValue(), &One)) : FText::GetEmpty();
        auto Actions = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Copy]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StyledButton(LOCTEXT("Easier", "더 쉽게"), TEXT("Tutor.GhostButton"), [this] { ChangeHint(Hint - 1, true); },
                [this, CanReask] { return CanReask() && Hint > 0; }, nullptr, LOCTEXT("EasierTip", "도움 수준을 한 단계 낮추고 같은 문제를 다시 물어봐요."))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StyledButton(LOCTEXT("MoreDetail", "더 자세히"), TEXT("Tutor.GhostButton"), [this] { ChangeHint(Hint + 1, true); },
                [this, CanReask] { return CanReask() && Hint < 3; }, nullptr, LOCTEXT("MoreDetailTip", "도움 수준을 한 단계 올리고 같은 문제를 다시 물어봐요."))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).WidthOverride(1.0f).HeightOverride(14.0f)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Rule")))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[IconButton(Style->GetBrush(TEXT("Icon.ThumbUp")), LOCTEXT("Helpful", "도움됐어요 · 이 PC에만 기록"), [Rate] { Rate(TEXT("up")); }, [this] { return !bMock; }, RatingTint(TEXT("up")))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[IconButton(Style->GetBrush(TEXT("Icon.ThumbDown")), LOCTEXT("NotHelpful", "아쉬워요 · 이 PC에만 기록"), [Rate] { Rate(TEXT("down")); }, [this] { return !bMock; }, RatingTint(TEXT("down")))]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpacer)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[SNew(STextBlock).Text(CreditText).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Micro")))
                .ToolTipText(LOCTEXT("AnswerCreditsTip", "이 답변을 만드는 데 쓴 추정 크레딧이에요. 실제 차감은 ChatKHU 기준이에요."))
                .Visibility(Credits.IsSet() ? EVisibility::Visible : EVisibility::Collapsed)];
        Conversation->AddSlot().Padding(14, 10, 14, 4)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[HeaderRow]
            + SVerticalBox::Slot().AutoHeight().Padding(30, 6, 4, 0)[Body]
            + SVerticalBox::Slot().AutoHeight().Padding(24, 4, 0, 0)[Actions]];
    }
    Conversation->ScrollToEnd();
}
void SKHUTutorPanel::AddNotice(ENotice Kind, const FString& Text, TArray<FNoticeAction> Actions)
{
    if (!Conversation || bStopped) { return; }
    if (++MessageCount > 80) { ClearConversation(); Links.Reset(); CurrentLinkIds.Reset(); MessageCount = 1; }
    if (Kind == ENotice::Divider)
    {
        Conversation->AddSlot().Padding(14, 8)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Rule")))]]
            + SHorizontalBox::Slot().FillWidth(4).VAlign(VAlign_Center).Padding(8, 0)[SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true).Justification(ETextJustify::Center)
                .TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Micro")))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Rule")))]]];
        Conversation->ScrollToEnd(); return;
    }
    const TCHAR* Brush = Kind == ENotice::Success ? TEXT("Tutor.Notice.Success") : Kind == ENotice::Warning ? TEXT("Tutor.Notice.Warning") : Kind == ENotice::Error ? TEXT("Tutor.Notice.Error") : TEXT("Tutor.Notice.Info");
    const TCHAR* IconName = Kind == ENotice::Success ? TEXT("Icons.Check") : Kind == ENotice::Warning ? TEXT("Icons.Warning") : Kind == ENotice::Error ? TEXT("Icons.Error") : TEXT("Icons.Info");
    const FLinearColor Tint = Kind == ENotice::Success ? KHUTutorPalette::Accent() : Kind == ENotice::Warning ? KHUTutorPalette::Warning() : Kind == ENotice::Error ? KHUTutorPalette::Error() : KHUTutorPalette::Link();
    auto Column = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Body")))];
    if (!Actions.IsEmpty())
    {
        auto Row = SNew(SWrapBox).UseAllottedSize(true);
        for (int32 I = 0; I < Actions.Num(); ++I)
        { Row->AddSlot().Padding(0, 8, 6, 0)[StyledButton(Actions[I].Label, I == 0 ? TEXT("Tutor.PrimaryButton") : TEXT("Tutor.SecondaryButton"), Actions[I].Run, Actions[I].Enabled)]; }
        Column->AddSlot().AutoHeight()[Row];
    }
    Conversation->AddSlot().Padding(14, 6)[SNew(SBorder).BorderImage(Style->GetBrush(Brush)).Padding(FMargin(10, 9))
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 8, 0)[SNew(SBox).WidthOverride(14).HeightOverride(14)
                [SNew(SImage).Image(FAppStyle::GetBrush(IconName)).ColorAndOpacity(FSlateColor(Tint))]]
            + SHorizontalBox::Slot().FillWidth(1)[Column]]];
    Conversation->ScrollToEnd();
}
void SKHUTutorPanel::BeginActivity()
{
    if (!Conversation || bStopped) { return; }
    if (Activity) { Activity->bLive = false; }
    TSharedRef<FTurnActivity> A = MakeShared<FTurnActivity>(); A->Start = FPlatformTime::Seconds();
    A->Step = LOCTEXT("ActivityStart", "질문과 현재 에디터 근거를 보내고 있어요.").ToString(); Activity = A;
    Conversation->AddSlot().Padding(14, 4)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Evidence"))).Padding(FMargin(10, 7))
        .Visibility_Lambda([A] { return A->bLive || A->Count > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox).Visibility_Lambda([A] { return A->bLive ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SCircularThrobber).Radius(6.0f).NumPieces(6).Period(0.9f).ColorAndOpacity(FSlateColor(KHUTutorPalette::Accent()))]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Label")))
                    .Text_Lambda([A] { return FText::Format(LOCTEXT("ActivityLive", "진행 중 · {0}개 확인 · {1}초"), FText::AsNumber(A->Count), FText::AsNumber(FMath::FloorToInt(FPlatformTime::Seconds() - A->Start))); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(22, 2, 0, 0)[SNew(STextBlock).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))
                .Visibility_Lambda([A] { return A->bLive ? EVisibility::Visible : EVisibility::Collapsed; }).Text_Lambda([A] { return FText::FromString(A->Step); })]
            + SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea).InitiallyCollapsed(true)
                .Visibility_Lambda([A] { return !A->bLive && A->Count > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                .BorderImage(Style->GetBrush(TEXT("Tutor.CoachBody"))).BodyBorderImage(Style->GetBrush(TEXT("Tutor.CoachBody")))
                .HeaderPadding(FMargin(0, 1)).Padding(FMargin(0, 6, 0, 0))
                .HeaderContent()[SNew(STextBlock).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Label")))
                    .Text_Lambda([A] { return FText::Format(LOCTEXT("ActivityDone", "확인한 근거 {0}개 · 에디터에서 직접 읽었어요"), FText::AsNumber(A->Count)); })]
                .BodyContent()[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SAssignNew(A->Details, SVerticalBox).Visibility_Lambda([this] { return Hint > 0 ? EVisibility::Visible : EVisibility::Collapsed; })]
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))
                        .Visibility_Lambda([this] { return Hint == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                        .Text(LOCTEXT("ActivityHidden", "L0에서는 읽은 내용을 숨겨요. 도움 수준을 올리면 볼 수 있어요."))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SAssignNew(A->Pins, SWrapBox).UseAllottedSize(true)
                .Visibility_Lambda([A] { return A->Count > 0 ? EVisibility::Visible : EVisibility::Collapsed; })]]];
    Conversation->ScrollToEnd();
}
void SKHUTutorPanel::AddCoachCard(const FKHULearningCoach& Value)
{
    if (!Conversation || (Value.Id().IsEmpty() && !Value.bPaused)) { return; }
    ShownReview = Value.Id();
    const FString Task = ActiveConversation;
    auto Card = MakeShared<FKHULearningCoach>(); Card->Restore(Value.Save());
    // While this is still the loop review, show its current choices; an older card keeps its last copy.
    auto Live = [this, Card]() -> const FKHULearningCoach& { return Loop && Loop->LearningCoach().Id() == Card->Id() ? Loop->LearningCoach() : *Card; };
    auto Active = [this, Task, Card]
    { return !Busy() && !bConversationReadOnly && ActiveConversation == Task
        && (!Loop || Loop->LearningCoach().Id() == Card->Id()); };
    auto Choose = [this, Task, Card](const FString& Choice)
    {
        if (Busy() || bConversationReadOnly || ActiveConversation != Task) { return; }
        bool Saved = false;
        if (Loop)
        { Saved = Loop->ChooseCoaching(Choice, Card->Id()); if (Saved) { Card->Restore(Loop->LearningCoach().Save()); } }
        else if (ProjectMemory)
        {
            FKHUSavedConversation Current;
            if (ProjectMemory->ReadConversation(Task, Hint, Current))
            { FKHULearningCoach Next; if (Next.Restore(Current.Coach) && Next.Id() == Card->Id() && Next.Choose(Choice))
              { Saved = ProjectMemory->SaveCoach(Task, AssetScope(), Next.Save()); if (Saved) { Card->Restore(Next.Save()); StoredConversation.Coach = Next.Save(); } } }
        }
        if (!Saved) { Status = TEXT("선택을 저장하지 못했어요. 이전 선택을 유지했으니 다시 확인해 주세요."); return; }
        if (Choice == TEXT("hint")) { ShowPage(2); ConnectionMessage = TEXT("원하는 도움 수준을 직접 선택한 뒤 대화로 돌아오세요."); }
        else { Status = TEXT("선택을 저장했어요. 질문은 계속 보낼 수 있어요."); }
    };
    const auto HasAction = [this, Live] { return Live().Action(Hint).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; };
    const auto TextStyle = [this](const TCHAR* Name) { return &Style->GetWidgetStyle<FTextBlockStyle>(Name); };
    Conversation->AddSlot().Padding(14, 4)[SNew(SExpandableArea).InitiallyCollapsed(true)
        .BorderImage(Style->GetBrush(TEXT("Tutor.Coach"))).BodyBorderImage(Style->GetBrush(TEXT("Tutor.CoachBody")))
        .HeaderPadding(FMargin(10, 8)).Padding(FMargin(12, 0, 12, 12))
        .HeaderContent()[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SImage).Image(Style->GetBrush(TEXT("Tutor.Dot"))).ColorAndOpacity(FSlateColor(KHUTutorPalette::Accent()))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("QuestionReview", "질문 돌아보기")).TextStyle(TextStyle(TEXT("Tutor.Strong")))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(STextBlock).Text(LOCTEXT("QuestionReviewTag", "점수가 아니라 다음 질문을 돕는 메모예요"))
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).TextStyle(TextStyle(TEXT("Tutor.Caption")))]]
        .BodyContent()[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(STextBlock).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(TextStyle(TEXT("Tutor.Body")))
                .Visibility_Lambda([this, Live] { return Live().Describe(Hint).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                .Text_Lambda([this, Live] { return FText::FromString(Live().Describe(Hint)); })]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(STextBlock).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Caption")))
                .Visibility_Lambda([this, Live] { return Live().Notice(Hint).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                .Text_Lambda([this, Live] { return FText::FromString(Live().Notice(Hint)); })]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Inset"))).Padding(FMargin(12, 10)).Visibility_Lambda(HasAction)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[SNew(STextBlock).Text(LOCTEXT("CoachSuggestion", "보완 제안")).TextStyle(TextStyle(TEXT("Tutor.WarningLabel")))]
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).LineHeightPercentage(1.4f).TextStyle(TextStyle(TEXT("Tutor.Body")))
                        .Text_Lambda([this, Live] { return FText::FromString(Live().Action(Hint)); })]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)[SNew(STextBlock).AutoWrapText(true).TextStyle(TextStyle(TEXT("Tutor.Caption"))).Visibility_Lambda(HasAction)
                .Text(LOCTEXT("CoachChoiceNote", "지금 어떻게 할까요? 선택은 완료 표시가 아니라 다음 안내 방식이에요."))]
            + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).Visibility_Lambda(HasAction)
                + SWrapBox::Slot().Padding(0, 4, 6, 0)[StyledButton(LOCTEXT("CoachTry", "직접 확인해 볼게요"), TEXT("Tutor.PrimaryButton"), [Choose] { Choose(TEXT("try")); }, Active)]
                + SWrapBox::Slot().Padding(0, 4, 6, 0)[StyledButton(LOCTEXT("CoachHint", "힌트를 더 받을게요"), TEXT("Tutor.SecondaryButton"), [Choose] { Choose(TEXT("hint")); }, Active)]
                + SWrapBox::Slot().Padding(0, 4, 6, 0)[StyledButton(LOCTEXT("CoachSkip", "지금은 넘어갈게요"), TEXT("Tutor.GhostButton"), [Choose] { Choose(TEXT("skip")); }, Active)]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 6, 0, 0)[SNew(SButton).ButtonStyle(&Style->GetWidgetStyle<FButtonStyle>(TEXT("Tutor.GhostButton"))).ContentPadding(FMargin(4, 3))
                .IsEnabled_Lambda(Active).OnClicked_Lambda([Live, Choose] { Choose(Live().bPaused ? TEXT("resume") : TEXT("pause")); return FReply::Handled(); })
                [SNew(STextBlock).TextStyle(TextStyle(TEXT("Tutor.Micro")))
                    .Text_Lambda([Live] { return Live().bPaused ? LOCTEXT("CoachResume", "자동 코칭 다시 켜기") : LOCTEXT("CoachPause", "이번 대화의 자동 코칭 일시 중지"); })]]]];
}
void SKHUTutorPanel::AddActorPathLink(const FString& Path)
{
    auto* Actor = FindObject<AActor>(nullptr, *Path); if (!IsValid(Actor) || Actor->GetPathName() != Path) { return; }
    FLink Link; Link.Native = TEXT("KHUTutorTools.KHUTutorShowToolset.FocusActor"); Link.Args = MakeShared<FJsonObject>();
    Link.Args->SetStringField(TEXT("actorLabel"), Path); Link.Actor = Actor; Link.bActorPath = true;
    CurrentLinkIds.Add(TEXT("@") + Path, FString::FromInt(Links.Add(MoveTemp(Link))));
}
void SKHUTutorPanel::AddReceipts()
{
    if (!Loop) { return; }
    const auto Receipt = Loop->Receipt(); TaskCredits.Reset();
    if (Receipt->GetBoolField(TEXT("cost_known"))) { TaskCredits = Receipt->GetNumberField(TEXT("budget_reserved_or_estimated")); }
    const auto& Events = Receipt->GetArrayField(TEXT("tools"));
    if (ReceiptCount < Events.Num() && !Activity) { BeginActivity(); }
    const TMap<FString, FString> ToolLabels = {
        {TEXT("Ping"), TEXT("프로젝트 연결")}, {TEXT("GetTutorContext"), TEXT("현재 작업 대상")},
        {TEXT("GetLogDigest"), TEXT("최근 로그")}, {TEXT("GetBlueprintCompileErrors"), TEXT("Blueprint 컴파일 오류")},
        {TEXT("GetCppBuildErrors"), TEXT("C++ 빌드 오류")}, {TEXT("ReadProjectSource"), TEXT("프로젝트 소스")},
        {TEXT("GetBlueprintGraphDigest"), TEXT("Graph 흐름")}, {TEXT("GetActorRelations"), TEXT("액터 참조 관계")},
        {TEXT("GetObjectDetails"), TEXT("Details 값")}, {TEXT("FocusActor"), TEXT("액터 위치 이동")},
        {TEXT("OpenAssetEditor"), TEXT("자산 열기")}, {TEXT("FocusGraphNode"), TEXT("노드 위치 이동")}};
    for (; ReceiptCount < Events.Num(); ++ReceiptCount)
    {
        const auto E = Events[ReceiptCount]->AsObject(); FString Native, Result, ArgsText;
        E->TryGetStringField(TEXT("native"), Native); E->TryGetStringField(TEXT("result"), Result); E->TryGetStringField(TEXT("arguments"), ArgsText);
        FString ShortName; Native.Split(TEXT("."), nullptr, &ShortName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        const bool Success = E->GetBoolField(TEXT("success"));
        if (Success) { ToolStatus = LOCTEXT("LocalReady", "정상 · 내부 실행").ToString(); }
        const FString* Friendly = ToolLabels.Find(ShortName); const FText Label = FText::FromString(Friendly ? *Friendly : ShortName);
        const FText Outcome = Success ? LOCTEXT("Ok", "완료") : LOCTEXT("Fail", "실패");
        if (const TSharedPtr<FTurnActivity> A = Activity)
        {
            ++A->Count; A->Step = FText::Format(LOCTEXT("ActivityStep", "{0} 확인 {1}"), Label, Outcome).ToString();
            if (A->Pins)
            {
                A->Pins->AddSlot().Padding(0, 0, 5, 5)[SNew(SBorder).BorderImage(Style->GetBrush(TEXT("Tutor.Pin"))).Padding(FMargin(7, 2, 9, 2))
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)[SNew(SBox).WidthOverride(11).HeightOverride(11)
                            [SNew(SImage).Image(FAppStyle::GetBrush(Success ? TEXT("Icons.Check") : TEXT("Icons.X"))).ColorAndOpacity(FSlateColor(Success ? KHUTutorPalette::Accent() : KHUTutorPalette::Error()))]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(Label).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]]];
            }
            if (A->Details)
            {
                A->Details->AddSlot().AutoHeight().Padding(0, 4)[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::Format(LOCTEXT("ToolEvidence", "확인한 근거 · {0} · {1}"), Label, Outcome)).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Label")))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Text(FText::FromString(Result)).AutoWrapText(true).TextStyle(&Style->GetWidgetStyle<FTextBlockStyle>(TEXT("Tutor.Caption")))]];
            }
        }
        TSharedPtr<FJsonObject> Args;
        if (!Success || !KHUMcpProtocol::ReadObject(ArgsText, Args)) { continue; }
        if (Native.EndsWith(TEXT(".GetActorRelations")) || Native.EndsWith(TEXT(".GetObjectDetails")))
        {
            TSharedPtr<FJsonObject> Snapshot; FString Scope;
            if (KHUMcpProtocol::ReadObject(Result, Snapshot) && Snapshot->TryGetStringField(TEXT("scope"), Scope) && Scope == TEXT("Editor"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
                if (Snapshot->TryGetArrayField(TEXT("objects"), Objects))
                { for (const auto& V : *Objects) { FString Path; if (V->AsObject()->TryGetStringField(TEXT("path"), Path)) { AddActorPathLink(Path); } } }
                const TSharedPtr<FJsonObject>* Object = nullptr; FString Path;
                if (Snapshot->TryGetObjectField(TEXT("object"), Object) && (*Object)->TryGetStringField(TEXT("path"), Path)) { AddActorPathLink(Path); }
            }
        }
        if (Native.EndsWith(TEXT(".GetTutorContext")))
        {
            const FString Marker = TEXT("\nSELECTED_ACTORS "); const int32 Start = Result.Find(Marker);
            TSharedPtr<FJsonObject> Selection; const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
            if (Start != INDEX_NONE && KHUMcpProtocol::ReadObject(Result.Mid(Start + Marker.Len()), Selection) && Selection->TryGetArrayField(TEXT("actors"), Actors))
            {
                TMap<FString, int32> Counts;
                for (const auto& V : *Actors) { if (V->Type == EJson::Object) { FString ActorLabel; V->AsObject()->TryGetStringField(TEXT("label"), ActorLabel); Counts.FindOrAdd(ActorLabel)++; } }
                for (const auto& V : *Actors)
                {
                    if (V->Type != EJson::Object) { continue; } FString ActorLabel, Name;
                    if (!V->AsObject()->TryGetStringField(TEXT("label"), ActorLabel) || !V->AsObject()->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty()) { continue; }
                    FLink Link; Link.Native = TEXT("KHUTutorTools.KHUTutorShowToolset.FocusActor"); Link.Args = MakeShared<FJsonObject>(); Link.Args->SetStringField(TEXT("actorLabel"), Name);
                    FString ActorPath;
                    if (V->AsObject()->TryGetStringField(TEXT("path"), ActorPath))
                    { auto* Actor = FindObject<AActor>(nullptr, *ActorPath); if (IsValid(Actor) && Actor->GetPathName() == ActorPath) { Link.Args->SetStringField(TEXT("actorLabel"), ActorPath); Link.Actor = Actor; Link.bActorPath = true; } }
                    const FString Id = FString::FromInt(Links.Add(MoveTemp(Link))); CurrentLinkIds.Add(TEXT("@") + Name, Id);
                    if (!ActorLabel.IsEmpty() && Counts[ActorLabel] == 1) { CurrentLinkIds.Add(TEXT("@") + ActorLabel, Id); }
                }
            }
        }
        if (Native.EndsWith(TEXT(".GetBlueprintGraphDigest")))
        {
            FString Asset, Issue; Args->TryGetStringField(TEXT("assetPath"), Asset);
            auto* BP = FKHUGraphDigest::FindLoadedBlueprint(Asset, Issue); if (!BP) { continue; }
            FString GraphName; const int32 NodesAt = Result.Find(TEXT(" nodes="));
            if (NodesAt != INDEX_NONE) { Result.Left(NodesAt).Split(TEXT(":"), nullptr, &GraphName, ESearchCase::CaseSensitive, ESearchDir::FromEnd); }
            auto* Graph = FKHUGraphDigest::ChooseGraph(BP, GraphName);
            if (GraphName.IsEmpty() || !Graph) { continue; }
            auto Evidence = MakeShared<FKHUEditorObservation>(BP, Graph, false, TArray<UEdGraphNode*>(), Result);
            CaptureLinks(Asset, Result, Evidence);
        }
    }
}
void SKHUTutorPanel::Navigate(const FString& Id)
{
    if (bStopped || Busy() || Hint == 0 || !Id.IsNumeric()) { return; } const int32 Index = FCString::Atoi(*Id); if (!Links.IsValidIndex(Index)) { return; }
    const auto Link = Links[Index];
    if (Link.bActorPath && (!Link.Actor.IsValid() || Link.Actor->GetPathName() != Link.Args->GetStringField(TEXT("actorLabel"))))
    { AddNotice(ENotice::Warning, TEXT("이 액터는 삭제되었거나 경로가 바뀌었어요. 다시 읽어 주세요.")); return; }
    if (Link.Observation && !Link.Observation->IsCurrent())
    {
        AddNotice(ENotice::Warning, LOCTEXT("StaleLink", "그래프가 바뀌어서 이 근거 링크는 만료됐어요. ‘수정했어요, 재확인’으로 새 근거를 읽어 주세요.").ToString(),
            {{LOCTEXT("ChipRecheck", "수정했어요, 재확인"), [this] { RunQuick(EQuick::Recheck); }, [this] { return bHasTask && !Busy(); }}});
        return;
    }
    const TWeakPtr<SKHUTutorPanel> Weak = SharedThis(this);
    Tools->Execute(Link.Native, Link.Args.ToSharedRef(), [Weak](FKHUMcpResult R)
    { if (auto P = Weak.Pin(); P && !P->bStopped)
      { P->ToolStatus = R.bSuccess ? LOCTEXT("LocalReady", "정상 · 내부 실행").ToString() : LOCTEXT("ToolFailed", "도구 실행 실패").ToString();
        P->AddNotice(R.bSuccess ? ENotice::Success : ENotice::Error, R.bSuccess ? LOCTEXT("Focused", "해당 위치로 이동했어요.").ToString() : R.Error); } });
}
void SKHUTutorPanel::Diagnose()
{
    if (bDiagnosing || bStopped) { return; } bDiagnosing = true; bApiConnected = false; const uint64 G = ++DiagnosticGeneration;
    ConnectionMessage = LOCTEXT("SettingsCheckingMessage", "연결을 확인하고 있어요. 잠시만 기다려 주세요.").ToString();
    Config = IKHUTutorModule::Get().PanelConfig(); Tools->Cancel(); Tools = IKHUTutorModule::Get().CreatePanelTools();
    Diagnostics.Reset(); Balance.Reset();
    for (const TCHAR* Name : {TEXT("KHUTutor"), TEXT("ToolsetRegistry")})
    { const auto Plugin = IPluginManager::Get().FindPlugin(Name); Diagnostics += FString(Name) + TEXT(": ") + (Plugin && Plugin->IsEnabled() ? TEXT("활성") : TEXT("비활성/미설치")) + TEXT("\n"); }
    Diagnostics += TEXT("에디터 도구: 플러그인 내부 실행 (MCP 서버 불필요)\n");
    const TWeakPtr<SKHUTutorPanel> Weak = SharedThis(this);
    ToolStatus = LOCTEXT("Checking", "확인 중").ToString();
    auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("message"), TEXT("panel-local-diagnosis"));
    Tools->Execute(FKHUMcpClient::PingToolName(), Args, [Weak, G](FKHUMcpResult R)
    {
        auto P = Weak.Pin(); if (!P || P->bStopped || P->DiagnosticGeneration != G) { return; }
        P->ToolStatus = R.bSuccess ? LOCTEXT("LocalReady", "정상 · 내부 실행").ToString() : LOCTEXT("ToolFailed", "도구 실행 실패").ToString();
        if (!R.bSuccess) { P->Diagnostics += R.Error + TEXT("\n"); }
        else { P->Diagnostics += R.Text + TEXT("\n"); }
        FKHUToolBridge Bridge(P->Config, FKHUToolExecutorDirect::Catalog());
        P->Diagnostics += FString::Printf(TEXT("허용 도구: %d개 (L1 기준)\n"), Bridge.Definitions(TEXT("T1"), 1, true).Num());
        P->QueryGateway(TEXT("models/"), G);
    });
}
void SKHUTutorPanel::QueryGateway(const FString& Path, uint64 G)
{
    FString Key, Error;
    if (!Config.bValid || !Secret().Load(Key, Error))
    { KeyStatus = LOCTEXT("NoKey", "키/설정 확인 필요").ToString(); bDiagnosing = false; Diagnostics += TEXT("\n") + Error;
      ConnectionMessage = !Config.bValid ? LOCTEXT("SettingsConfigIssue", "연결 설정을 읽지 못했어요. 고급 설정을 확인해 주세요.").ToString()
          : LOCTEXT("SettingsMissingKey", "사용할 수 있는 키가 없어요. API 키를 입력하고 저장해 주세요.").ToString(); Status = ConnectionMessage; ShowPage(2); return; }
    FKHUMcpHttpRequest R; R.Verb = TEXT("GET"); R.Url = Config.BaseUrl + TEXT("/") + Path;
    R.Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + Key); R.Headers.Add(TEXT("User-Agent"), TEXT("KHU-UE-Tutor-D8/0.1"));
    const TWeakPtr<SKHUTutorPanel> Weak = SharedThis(this);
    CancelDiagnostic = DiagnosticTransport->Send(R, [Weak, G, Path](FKHUMcpHttpResponse Reply)
    {
        auto P = Weak.Pin(); if (!P || P->bStopped || P->DiagnosticGeneration != G) { return; }
        TSharedPtr<FJsonObject> O; const bool Ok = Reply.Status == 200 && KHUMcpProtocol::ReadObject(Reply.Body, O);
        if (Path == TEXT("models/"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
            const bool Valid = Ok && O->TryGetArrayField(TEXT("data"), Models);
            P->bApiConnected = Valid && P->ToolStatus == LOCTEXT("LocalReady", "정상 · 내부 실행").ToString();
            P->KeyStatus = Valid ? LOCTEXT("ValidKey", "인증 성공").ToString() : LOCTEXT("InvalidKey", "인증 미확인").ToString();
            P->Diagnostics += TEXT("ChatKHU: ") + P->KeyStatus + TEXT("\n");
            if (Valid) { P->QueryGateway(TEXT("credits/"), G); return; }
        }
        else
        {
            const TSharedPtr<FJsonObject>* Total = nullptr; double Remaining;
            if (Ok && O->TryGetObjectField(TEXT("total"), Total) && (*Total)->TryGetNumberField(TEXT("remaining"), Remaining) && FMath::IsFinite(Remaining) && Remaining >= 0) { P->Balance = Remaining; }
            P->Diagnostics += P->Balance.IsSet() ? FString::Printf(TEXT("계정 잔액: %.2f 크레딧 (조회 시점)\n"), P->Balance.GetValue()) : TEXT("계정 잔액: 미확인\n");
        }
        if (!Ok) { P->Diagnostics += KHUChatProtocol::Error(Reply.Status); }
        P->bDiagnosing = false;
        if (P->bApiConnected) { P->Status = LOCTEXT("ConnectionReady", "연결 완료 · 질문을 보내 주세요.").ToString();
            P->ConnectionMessage = LOCTEXT("SettingsReadyMessage", "연결됐어요. 대화 화면에서 질문을 시작하세요.").ToString(); P->ShowPage(0); }
        else { P->Status = LOCTEXT("ConnectionFailed", "연결을 확인하지 못했어요. 키와 연결 상태를 확인해 주세요.").ToString();
            P->ConnectionMessage = LOCTEXT("SettingsRetryMessage", "연결을 확인하지 못했어요. 키와 인터넷 연결을 확인한 뒤 다시 시도해 주세요.").ToString(); P->ShowPage(2); }
    });
}
#undef LOCTEXT_NAMESPACE

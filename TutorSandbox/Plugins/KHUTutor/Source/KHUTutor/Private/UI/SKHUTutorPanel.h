#pragma once
#include "Widgets/SCompoundWidget.h"
#include "AI/KHUAgentLoop.h"
#include "AI/KHUHintPolicy.h"
#include "Styling/SlateStyle.h"

class FBlueprintEditor;
class UBlueprint;
class UEdGraph;
class FKHUEditorObservation;
class FKHUFeedbackStore;
class SScrollBox;
class SMultiLineEditableTextBox;
class SEditableTextBox;
class SWidgetSwitcher;
class SVerticalBox;
class SBorder;
class SWrapBox;
class FMenuBuilder;
struct FSlateBrush;

/** A bounded conversation view; asynchronous callbacks hold only weak widgets. */
class SKHUTutorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SKHUTutorPanel) {}
        SLATE_ARGUMENT(TWeakPtr<FBlueprintEditor>, BlueprintEditor)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SKHUTutorPanel() override;
    /** Cancel work and release credentials before tab/module shutdown. */
    void Stop();
private:
    friend class FU1PanelState;
    friend class FSetupMemoryPanel;
    friend class FChatSidebarPages;
    friend class FChatSidebarResume;
    friend class FPauseNoticeButtons;
    friend class FChatSidebarMessages;
    friend class FProjectManagerFlow;
    friend class FCoachPanel;
    friend class FFeedbackPanel;
    friend class FCppPracticePanel;
    friend class FPortfolioPanel;
    friend class FPortfolioBlindLive;
    friend class FStuckHintOffer;
    friend class FSessionNavigation;
    friend class FComposerDraftNotice;
    friend class FRecordsHub;
    struct FRecordDraft { FString Context, Title, Revision; int32 Hint = 1, Count = 0; };
    struct FLink { FString Native; TSharedPtr<FJsonObject> Args; TSharedPtr<FKHUEditorObservation> Observation; TWeakObjectPtr<UObject> Actor; bool bActorPath = false; };
    /** Progress while a turn runs, then the folded evidence that turn read. Display only. */
    struct FTurnActivity { TSharedPtr<SWrapBox> Pins; TSharedPtr<SVerticalBox> Details; int32 Count = 0; double Start = 0; bool bLive = true; FString Step; };
    enum class ENotice : uint8 { Info, Success, Warning, Error, Divider };
    struct FNoticeAction { FText Label; TFunction<void()> Run; TFunction<bool()> Enabled; };
    enum class EQuick : uint8 { Errors, Selection, Practice, Recheck, ExplainBack, Relations, Where };

    TSharedRef<SWidget> Button(const FText& Label, TFunction<void()> Action, TFunction<bool()> Enabled = {});
    TSharedRef<SWidget> SettingsButton(const FText& Label, TFunction<void()> Action, FName Appearance, TFunction<bool()> Enabled = {});
    TSharedRef<SWidget> StyledButton(const FText& Label, FName Appearance, TFunction<void()> Action, TFunction<bool()> Enabled = {}, const FSlateBrush* Icon = nullptr, const FText& Tip = FText::GetEmpty());
    TSharedRef<SWidget> IconButton(const FSlateBrush* Icon, const FText& Tip, TFunction<void()> Action, TFunction<bool()> Enabled = {}, TFunction<FSlateColor()> Tint = {});
    TSharedRef<SWidget> BuildHeader();
    TSharedRef<SWidget> BuildContextBar();
    TSharedRef<SWidget> BuildInputArea();
    TSharedRef<SWidget> BuildEmptyState();
    TSharedRef<SWidget> HelperMenu();
    TSharedRef<SWidget> PracticeMenu();
    void AddPracticeEntries(FMenuBuilder& Menu);
    void DraftPractice(const FString& Id, bool bTransfer);
    bool InsertDraft(const FText& Draft, const FString& Message);
    TSharedRef<SWidget> HintMenu();
    TSharedRef<SWidget> ContextMenu();
    TSharedRef<SWidget> BuildSettingsPage();
    TSharedRef<SWidget> BuildProjectsPage();
    TSharedRef<SWidget> BuildSessionSidebar();
    TSharedRef<SWidget> BuildRecordsPage();
    void RefreshRecords();
    void OpenRecords();
    bool SaveCurrentDraft();
    TSharedRef<SWidget> BuildPortfolioPage();
    void OpenPortfolio(const FString& Id);
    void RefreshPortfolioSources();
    void GeneratePortfolio();
    bool SavePortfolio();
    bool PortfolioAllowed() const;
    bool PortfolioExport(FString& Text);
    void PreparePortfolioPrompt();
    TSharedRef<SWidget> ProjectMenu(const FString& Id, bool bFolder);
    void RunQuick(EQuick Kind);
    void RefreshProjects();
    void NewConversation(const FString& Folder);
    void OpenConversation(const FString& Id);
    void RenderStoredConversation(int32 Start);
    void EditProjectName(const FString& Kind, const FString& Id, const FString& Title);
    void SaveProjectName();
    FString AssetScope() const;
    void Submit(const FString& Question, bool bAttachGraph = true);
    bool CaptureContext();
    void CaptureLinks(const FString& Asset, const FString& Digest, TSharedPtr<FKHUEditorObservation> Evidence);
    FText ContextLabel() const;
    FText CompactContextLabel() const;
    FText ContextState() const;
    FText HeaderSubtitle() const;
    FText MetaLine() const;
    FSlateColor StatusColor() const;
    FString ModelLabel() const;
    static FText HintName(int32 Level);
    static FText HintDescription(int32 Level);
    void ShowPage(int32 Page);
    void CancelTask();
    FText ProjectProgressLabel() const;
    void RememberProgress();
    void ChangeHint(int32 Level, bool bRequest);
    void OnEvent(EKHUAgentState State, const FString& Text);
    void ClearConversation();
    void AddMessage(const FText& Who, const FString& Text, bool bLinks = false, int32 TurnHint = -1);
    void AddNotice(ENotice Kind, const FString& Text, TArray<FNoticeAction> Actions = {});
    void BeginActivity();
    void AddReceipts();
    void AddCoachCard(const FKHULearningCoach& Coach);
    void AddActorPathLink(const FString& Path);
    void ResetTask(bool bSolved);
    void Diagnose();
    void QueryGateway(const FString& Path, uint64 Generation);
    void Navigate(const FString& Id);
    bool Busy() const;
    FText Header() const;
    TWeakPtr<FBlueprintEditor> BlueprintEditor;
    TWeakObjectPtr<UBlueprint> TargetBlueprint;
    TWeakObjectPtr<UEdGraph> TaskGraph;
    TSharedPtr<FKHUEditorObservation> Observation;
    bool bBlueprintPanel = false, bSelectionOnly = true;
    TSharedPtr<SScrollBox> Conversation;
    TSharedPtr<SWidgetSwitcher> Pages;
    TSharedPtr<SMultiLineEditableTextBox> Input;
    TSharedPtr<SMultiLineEditableTextBox> MemoryInput;
    TSharedPtr<SEditableTextBox> KeyInput;
    TSharedPtr<SEditableTextBox> ProjectNameInput;
    TSharedPtr<SVerticalBox> ProjectList;
    TSharedPtr<SVerticalBox> SessionList;
    TSharedPtr<SVerticalBox> RecordList;
    TSharedPtr<SBorder> ComposerNotice;
    TSharedPtr<FSlateStyleSet> Style;
    TSharedPtr<FKHUAgentLoop> Loop;
    TSharedPtr<FKHUProjectMemory> ProjectMemory;
    TSharedPtr<FKHUFeedbackStore> Feedback;
    TSharedPtr<IKHUToolExecutor> Tools;
    TSharedPtr<IKHUMcpTransport> DiagnosticTransport;
    IKHUMcpTransport::FCancel CancelDiagnostic;
    TSharedPtr<FTurnActivity> Activity;
    TArray<FLink> Links;
    TMap<FString, FString> CurrentLinkIds;
    FKHUConfig Config;
    int32 Hint = 1, ReceiptCount = 0, MessageCount = 0, TurnCount = 0;
    int32 ActivePage = 0;
    bool bSessionSidebarVisible = true, bDraftConflict = false;
    bool bMock = false, bHasTask = false, bStopped = false, bDiagnosing = false;
    bool bApiConnected = false;
    int32 SeenSession = 1;
    uint64 DiagnosticGeneration = 0;
    FString Status, ToolStatus, KeyStatus, Diagnostics;
    FString ConnectionMessage;
    FString ActiveConversation, ActiveConversationTitle, ActiveFolder, ProjectQuery, EditingKind, EditingId, LibraryMessage;
    TMap<FString, FString> Drafts;
    /** The visible composer and its explicitly shown reference attachment travel together per chat. */
    TMap<FString, FRecordDraft> RecordDrafts;
    FString RecordQuery, RecordsMessage;
    int32 RecordPageStart = 0;
    bool bCanRecordActive = false;
    TMap<FString, int32> VisibleCounts;
    TMap<FString, bool> SessionFolderExpansion;
    FKHUSavedConversation StoredConversation;
    bool bShowArchived = false, bConversationReadOnly = false;
    bool bReleasingLoop = false;
    int32 HistoryStart = 0;
    FString MemoryKind = TEXT("decision");
    FString ShownReview;
    /** Consecutive "don't know" messages in this task; at L0 the panel offers, never applies, a higher hint. */
    FKHUStuckStreak Stuck;
    FKHUSavedConversation PortfolioConversation;
    FKHUPortfolio PortfolioRecord;
    TArray<FString> PortfolioSelection;
    TSharedPtr<SMultiLineEditableTextBox> PortfolioBody, PortfolioEvidence;
    TSharedPtr<SVerticalBox> PortfolioSources;
    FString PortfolioMessage;
    int32 PortfolioSourceStart = 0;
    int32 PortfolioStep = 0;
    bool bPortfolioDirty = false;
    TOptional<double> Balance, TaskCredits, PendingAnswerCredits;
    /** Usage events already attributed to a shown answer; per-answer credits sum only newer estimates. */
    int32 UsageSeen = 0;
};

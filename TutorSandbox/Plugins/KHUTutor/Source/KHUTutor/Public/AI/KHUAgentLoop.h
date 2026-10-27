#pragma once
#include "AI/KHUChatClient.h"
#include "AI/KHUToolBridge.h"
#include "AI/KHUToolExecutor.h"
#include "AI/KHUContextBudget.h"
#include "AI/KHUProjectMemory.h"
#include "AI/KHULearningCoach.h"

enum class EKHUAgentState { Idle, WaitingModel, ExecutingTool, AskContinue };
/** Why a task paused in AskContinue; only Iterations and SoftBudget can be resumed. */
enum class EKHUPauseReason : uint8 { None, Iterations, SoftBudget, HardBudget };
/** Receipt spelling of a pause reason; None is the empty string. */
KHUTUTOR_API const TCHAR* LexToString(EKHUPauseReason Reason);
/** One task, bounded history, serial tools, cancellation and resumable limits. */
class KHUTUTOR_API FKHUAgentLoop : public TSharedFromThis<FKHUAgentLoop>
{
public:
    using FEvent = TFunction<void(EKHUAgentState, const FString&)>;
    FKHUAgentLoop(FKHUConfig InConfig, TSharedRef<IKHUChatClient> InChat, TSharedRef<IKHUToolExecutor> InExecutor,
        TSharedRef<FKHUToolBridge> InBridge, FString CalibrationPath = TEXT(""));
    /** Start a task; optional record context is model-only, bounded to 32000 characters and retained for retries. */
    bool Ask(const FString& Question, const FString& Tier, int32 Hint, FEvent Event, bool bDevelopment = false, bool bAllowShow = false, const FString& SavedConversation = TEXT(""), const FString& RecordContext = TEXT(""));
    /** Continue the retained task after a model-iteration or soft-credit pause. */
    void Resume();
    /** Start a follow-up; omitted record context clears the previous turn's record mode. */
    bool FollowUp(const FString& Question, int32 Level, const FString& RecordContext = TEXT(""));
    /** Replace editor evidence for the next student turn; data, never system instructions. */
    void SetEditorObservation(const FString& Json) { EditorObservation = Json; }
    /** Opt-in project-local memory for product panels; console/tests remain isolated unless configured. */
    void SetProjectMemory(TSharedPtr<FKHUProjectMemory> Memory, const FString& AssetScope);
    /** Bounded, untrusted progress carried into the current conversation. */
    FString MemoryPreview() const { return CarriedMemory; }
    /** Current validated question review and explicit choice handling. */
    const FKHULearningCoach& LearningCoach() const { return Coach; }
    bool ChooseCoaching(const FString& Choice, const FString& ExpectedId);
    /** Record student's explicit solved status, never an AI success inference. */
    void MarkSolved();
    /** Cancel model and tool requests and discard every late completion. */
    void Cancel();
    EKHUAgentState State() const { return CurrentState; }
    /** Hard limits cannot be raised by Continue; iteration pauses do not grant credits. */
    bool CanResume() const;
    /** Usage and tool receipts; unknown costs are null, never silently zero. */
    TSharedRef<FJsonObject> Receipt() const;
private:
    void Send();
    void AddCoachSources();
    bool PersistCoach();
    FKHULearningCoach Coach;
    TArray<FKHUCoachSource> CoachSources;
    FString PriorCoachAI;
    int32 CoachToolStart = 0;
    void Received(uint64 G, FString Model, double Reserve, FKHUChatResponse Response);
    void NextTool(uint64 G);
    void ToolResult(uint64 G, FString Identity, FKHUMcpResult Result);
    void Notify(EKHUAgentState State, const FString& Text);
    void Pause(EKHUPauseReason Reason, const FString& Text);
    void SaveMemory(const FString& Suggestion, bool bSolved = false);
    void ArchiveStudent(int32 RecordSession = 0);
    FString StudentTurn(const FString& Question, bool bCarry) const;
    bool IsRecordTurn() const;
    FKHUConfig Config;
    FKHUTokenEstimator Estimator;
    TSharedRef<IKHUChatClient> Chat;
    TSharedRef<IKHUToolExecutor> Executor;
    TSharedRef<FKHUToolBridge> Bridge;
    FEvent Event;
    FKHUModelTier Tier;
    FString TierName, LastErrorIdentity, LastText, ActiveNative, ActiveArguments, TaskId, RequestId;
    EKHUPauseReason PauseReason = EKHUPauseReason::None;
    FString EditorObservation;
    TSharedPtr<FKHUProjectMemory> ProjectMemory;
    FString AssetScope, Goal, LatestStudent, LatestSuggestion, CarriedMemory;
    FString CurrentRecordContext;
    int32 Session = 1, CompletedSessionTurns = 0, LastInputTokens = 0;
    int32 StudentTurnNumber = 0;
    FString MemoryError;
    bool bMemorySaveFailed = false;
    TArray<TSharedPtr<FJsonValue>> Messages, UsageEvents, ToolEvents, ContextEvents, CancelledRequests;
    TArray<FKHUToolCall> PendingTools;
    int32 Hint = 0, ModelIndex = 0, WindowIters = 0, Iterations = 0, ToolIndex = 0, ErrorRepeats = 0;
    double SpentEstimate = 0, Allowance = 0, PendingReserve = 0;
    int32 OmittedTurns = 0;
    uint64 Generation = 0;
    bool bDevelopment = false, bAllowShow = false, bCostKnown = true;
    bool bPendingRequest = false;
    EKHUAgentState CurrentState = EKHUAgentState::Idle;
};

#include "AI/KHUAgentLoop.h"
#include "AI/KHUClaudeRuntime.h"
#include "AI/KHUHintPolicy.h"
#include "AI/KHUSecretStore.h"
#include "KHUTutorModule.h"
#include "UI/SKHUTutorPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    bool SaveBlindJson(const FString& Path, const TSharedRef<FJsonObject>& Object)
    { return FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Object), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); }

    /** Capture only the product request, not the separate examiner's rubric. */
    class FBlindRecorder final : public IKHUChatClient
    {
    public:
        TSharedRef<FKHUClaudeRuntime> Runtime;
        TSharedRef<FJsonObject> Sent = MakeShared<FJsonObject>();
        FString Raw;
        FBlindRecorder(const FKHUConfig& C, const FString& Key, TSharedPtr<FKHUProjectMemory> Memory)
            : Runtime(MakeShared<FKHUClaudeRuntime>(C, Key, Memory)) {}
        virtual void Send(FKHUChatRequest Request, FCallback Callback) override
        {
            Sent->SetStringField(TEXT("task_id"), Request.TaskId);
            Sent->SetStringField(TEXT("model"), Request.Model);
            Sent->SetNumberField(TEXT("hint"), Request.Hint);
            Sent->SetArrayField(TEXT("messages"), Request.Messages);
            Runtime->Send(MoveTemp(Request), [this, Callback = MoveTemp(Callback)](FKHUChatResponse Response) mutable
            { Raw = Response.Content; Callback(MoveTemp(Response)); });
        }
        virtual void Cancel() override { Runtime->Cancel(); }
        virtual bool IsMock() const override { return false; }
        virtual bool OwnsHistory() const override { return true; }
        virtual void ObserveTools(FToolObserver Observer) override { Runtime->ObserveTools(MoveTemp(Observer)); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortfolioBlindLive, "KHUTutor.Portfolio.Live.BlindE2E3",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPortfolioBlindLive::RunTest(const FString&)
{
    FString Spec;
    if (!FParse::Value(FCommandLine::Get(), TEXT("KHUTutorBlindLive="), Spec))
    { AddInfo(TEXT("Paid fresh-conversation blind test not requested")); return true; }
    FString Text, Key, Error; TSharedPtr<FJsonObject> Plan;
    if (!FFileHelper::LoadFileToString(Text, *Spec) || !KHUMcpProtocol::ReadObject(Text, Plan))
    { AddError(TEXT("Blind input plan unreadable")); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
    if (!Plan->TryGetArrayField(TEXT("cases"), Cases) || Cases->IsEmpty() || Cases->Num() > 4)
    { AddError(TEXT("Expected 1-4 fresh cases")); return false; }
    int32 FirstCase = 0;
    FParse::Value(FCommandLine::Get(), TEXT("KHUTutorBlindStart="), FirstCase);
    if (FirstCase < 0 || FirstCase >= Cases->Num()) { AddError(TEXT("Blind starting case is outside the frozen plan")); return false; }
    if (!FKHUSecretStore(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi")).Load(Key, Error))
    { AddError(Error); return false; }

    class FRun final : public IAutomationLatentCommand
    {
    public:
        FAutomationTestBase* Test;
        FKHUConfig C;
        FString Key, Out, Id, Question, RecordContext;
        TArray<TSharedPtr<FJsonValue>> Cases;
        TArray<FString> Turns;
        int32 Case = 0, Turn = 0, Hint = 1;
        bool bPortfolio = false, bDone = false;
        double Deadline = 0;
        TSharedPtr<FKHUProjectMemory> Memory;
        TSharedPtr<FKHUAgentLoop> Agent;
        TSharedPtr<FBlindRecorder> Client;
        TSharedPtr<SKHUTutorPanel> Panel;
        // Mirrors the panel: records whether the L0 hint-offer card would appear after this answer.
        FKHUStuckStreak Streak;

        FRun(FAutomationTestBase* T, FString K, const TArray<TSharedPtr<FJsonValue>>& Items, FString Directory, int32 First)
            : Test(T), C(IKHUTutorModule::Get().PanelConfig()), Key(MoveTemp(K)), Out(MoveTemp(Directory)), Cases(Items), Case(First)
        { C.Context.bProjectMemory = true; StartCase(); }
        ~FRun() override { if (Agent) { Agent->Cancel(); } if (Panel) { Panel->Stop(); } }
        bool Check(bool Ok, const TCHAR* Name)
        { Test->TestTrue(Name, Ok); if (!Ok) { bDone = true; } return Ok; }
        FString Output(const FString& Suffix) const
        { return Out / FString::Printf(TEXT("case_%d_%s"), Case, *Suffix); }
        void StartCase()
        {
            const auto Item = Cases[Case]->AsObject();
            Hint = static_cast<int32>(Item->GetNumberField(TEXT("hint")));
            Turns.Reset();
            for (const auto& V : Item->GetArrayField(TEXT("turns"))) { Turns.Add(V->AsString()); }
            if (!Check(Hint >= 0 && Hint <= 3 && !Turns.IsEmpty() && Turns.Num() <= 4, TEXT("Bounded blind inputs"))) { return; }
            const FString Path = Output(TEXT("memory.json"));
            if (!Check(!IFileManager::Get().FileExists(*Path), TEXT("No pre-existing memory is used"))) { return; }
            Memory = MakeShared<FKHUProjectMemory>(Path, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), C.Context);
            if (!Check(Memory->CreateConversation(TEXT(""), TEXT(""), Hint, Id), TEXT("Fresh random conversation created"))) { return; }
            Panel = SNew(SKHUTutorPanel); Panel->bMock = false; Panel->Hint = Hint; Panel->ProjectMemory = Memory;
            Turn = 0; bPortfolio = false; Question = Turns[0]; RecordContext.Reset(); Streak.Reset(); StartTurn();
        }
        void StartTurn()
        {
            Client = MakeShared<FBlindRecorder>(C, Key, Memory);
            Agent = MakeShared<FKHUAgentLoop>(C, Client.ToSharedRef(), MakeShared<FKHUToolExecutorDirect>(),
                MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()));
            Agent->SetProjectMemory(Memory, TEXT("")); Deadline = FPlatformTime::Seconds() + 360; if (!bPortfolio) { Streak.Note(Question); }
            Check(Agent->Ask(Question, TEXT("T1"), Hint, [](EKHUAgentState, const FString&) {}, true, false, Id, RecordContext), TEXT("Blind turn started"));
        }
        bool PreparePortfolio()
        {
            Panel->OpenPortfolio(Id);
            if (!Check(Panel->ActivePage == 3, TEXT("Actual E3 page opens for the fresh conversation"))) { return false; }
            Panel->PortfolioSelection.Reset();
            for (const auto& V : Panel->PortfolioConversation.Turns) { Panel->PortfolioSelection.Add(V->AsObject()->GetStringField(TEXT("id"))); }
            Panel->GeneratePortfolio();
            FString Export;
            if (!Check(!Panel->PortfolioBody->GetText().IsEmpty() && !Panel->PortfolioExport(Export), TEXT("Scaffold requires confirmation before export"))) { return false; }
            if (!Check(Panel->SavePortfolio(), TEXT("Fresh E2 scaffold saves"))) { return false; }
            Panel->PreparePortfolioPrompt(); Question = Panel->Input->GetText().ToString();
            if (!Check(Question == FKHUPortfolio::Question() && !Question.Contains(TEXT("<record_data>")), TEXT("Actual panel prepares a readable E3 question"))) { return false; }
            if (!Check(Panel->RecordDrafts.Contains(Id), TEXT("Actual panel separately attaches the selected record"))) { return false; }
            RecordContext = Panel->RecordDrafts.FindRef(Id).Context;
            if (!Check(RecordContext.Contains(TEXT("<record_data>")), TEXT("Attached source context is available to the model"))) { return false; }
            if (!Check(FFileHelper::SaveStringToFile(RecordContext, *Output(TEXT("portfolio_context.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("Exact attached E3 context retained separately"))) { return false; }
            return Check(FFileHelper::SaveStringToFile(Question, *Output(TEXT("portfolio_request.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("Generated E3 request retained"));
        }
        bool VerifyPortfolio(const FString& Answer)
        {
            // Simulate the student's paste and explicit review, without touching the OS clipboard or publishing.
            Panel->Input->SetText(FText::GetEmpty()); Panel->OpenPortfolio(Id);
            Panel->PortfolioBody->SetText(FText::FromString(Answer));
            Panel->PortfolioEvidence->SetText(FText::FromString(TEXT("합성 대화 기반 검사. 실제 프로젝트 실행·학생 역량 검증 자료가 아님.")));
            FString Export;
            if (!Check(!Panel->PortfolioExport(Export), TEXT("Pasted AI answer is unconfirmed"))) { return false; }
            Panel->PortfolioRecord.bConfirmed = true;
            if (!Check(Panel->PortfolioExport(Export), TEXT("Explicitly confirmed body and evidence produce copy text"))) { return false; }
            const FString Body = Panel->PortfolioRecord.Body;
            if (!Check(FFileHelper::SaveStringToFile(Export, *Output(TEXT("export.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("Exact copy output retained"))) { return false; }
            auto Reopened = MakeShared<FKHUProjectMemory>(Output(TEXT("memory.json")), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), C.Context);
            Panel->ProjectMemory = Reopened; Panel->OpenPortfolio(Id);
            if (!Check(Panel->PortfolioBody->GetText().ToString() == Body && Panel->PortfolioRecord.bConfirmed, TEXT("New storage reader preserves saved body and confirmation"))) { return false; }
            Panel->PortfolioBody->SetText(FText::FromString(Body + TEXT("\n편집 확인")));
            if (!Check(!Panel->PortfolioRecord.bConfirmed && !Panel->PortfolioExport(Export) && Export.IsEmpty(), TEXT("Editing revokes confirmation and export"))) { return false; }
            // Keep the verified disk revision unchanged; cancel only this test widget's unsaved edit.
            Panel->bPortfolioDirty = false; Panel->OpenPortfolio(Id); Panel->Stop(); Panel.Reset();
            return true;
        }
        virtual bool Update() override
        {
            if (bDone) { return true; }
            if (FPlatformTime::Seconds() > Deadline) { Agent->Cancel(); Check(false, TEXT("Blind turn completed before timeout")); return true; }
            if (Agent->State() != EKHUAgentState::Idle && Agent->State() != EKHUAgentState::AskContinue) { return false; }
            const auto R = Agent->Receipt();
            R->SetStringField(TEXT("question"), Question); R->SetStringField(TEXT("raw_response"), Client->Raw);
            if (bPortfolio) { R->SetStringField(TEXT("record_context"), RecordContext); }
            R->SetObjectField(TEXT("sent_request"), Client->Sent);
            R->SetStringField(TEXT("phase"), bPortfolio ? TEXT("E3") : TEXT("E2"));
            R->SetNumberField(TEXT("hint"), Hint); R->SetObjectField(TEXT("coach_snapshot"), Agent->LearningCoach().Save());
            R->SetStringField(TEXT("action_display"), Agent->LearningCoach().Action(Hint));
            R->SetBoolField(TEXT("app_more_help_card"), !bPortfolio && Streak.TakeOffer(Hint));
            const auto& Usage = R->GetArrayField(TEXT("usage"));
            const bool Success = Agent->State() == EKHUAgentState::Idle && !Usage.IsEmpty()
                && Usage.Last()->AsObject()->GetBoolField(TEXT("success")) && !R->GetStringField(TEXT("text")).TrimStartAndEnd().IsEmpty();
            if (!Check(SaveBlindJson(Output(FString::Printf(TEXT("turn_%d.json"), Turn)), R), TEXT("Unmodified answer and exact product input retained"))) { return true; }
            if (!Check(Success, TEXT("Paid blind response is nonempty and successful"))) { return true; }
            const FString Answer = R->GetStringField(TEXT("text")); Agent->Cancel(); Agent.Reset();
            if (bPortfolio)
            {
                if (!VerifyPortfolio(Answer)) { return true; }
                if (++Case == Cases.Num()) { bDone = true; return true; }
                StartCase(); return bDone;
            }
            if (++Turn < Turns.Num()) { Question = Turns[Turn]; StartTurn(); return bDone; }
            if (!PreparePortfolio()) { return true; }
            bPortfolio = true; StartTurn(); return bDone;
        }
    };
    AddCommand(new FRun(this, MoveTemp(Key), *Cases, FPaths::GetPath(FPaths::ConvertRelativePathToFull(Spec)), FirstCase)); return true;
}
#endif

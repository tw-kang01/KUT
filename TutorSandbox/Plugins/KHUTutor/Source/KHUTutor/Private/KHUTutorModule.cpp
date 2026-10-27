#include "KHUTutorModule.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonValue.h"
#include "AI/KHUSecretStore.h"
#include "UI/KHUTutorSettings.h"
#include "UI/KHUTutorUI.h"
#include "UI/KHUPanelMock.h"
#include "AI/KHUClaudeRuntime.h"

DEFINE_LOG_CATEGORY_STATIC(LogKHUTutorMcp, Log, All);

namespace
{
    void SaveReceipt(const FString& Name, const FKHUMcpResult& Result)
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("KHUTutor");
        IFileManager::Get().MakeDirectory(*Directory, true);
        auto Object = MakeShared<FJsonObject>();
        Object->SetBoolField(TEXT("success"), Result.bSuccess);
        Object->SetStringField(TEXT("error"), Result.Error);
        Object->SetStringField(TEXT("text"), Result.Text);
        Object->SetNumberField(TEXT("http_status"), Result.HttpStatus);
        if (Result.Object.IsValid()) { Object->SetObjectField(TEXT("result"), Result.Object); }
        FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Object), *(Directory / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    void Report(const TCHAR* Operation, const FString& File, FKHUMcpResult Result)
    {
        SaveReceipt(File, Result);
        if (Result.bSuccess) { UE_LOG(LogKHUTutorMcp, Display, TEXT("MCP %s succeeded: %s"), Operation, *Result.Text.Left(6000)); }
        else { UE_LOG(LogKHUTutorMcp, Error, TEXT("MCP %s failed: %s"), Operation, *Result.Error); }
    }
}

class FKHUTutorModule final : public IKHUTutorModule
{
public:
    virtual void StartupModule() override
    {
        FKHUMcpOptions Options;
        Options.ExpectedProject = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("KHUTutor"));
        if (Plugin.IsValid())
        {
            FString Text;
            TSharedPtr<FJsonObject> Object;
            if (FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Config/Mcp.json"))) && KHUMcpProtocol::ReadObject(Text, Object))
            {
                FString Url;
                if (Object->TryGetStringField(TEXT("url"), Url)) { Options.Url = Url; }
                double Timeout = 0;
                if (Object->TryGetNumberField(TEXT("control_timeout_seconds"), Timeout) && FMath::IsFinite(Timeout)) { Options.ControlTimeoutSeconds = FMath::Clamp(static_cast<float>(Timeout), 1.0f, 30.0f); }
                if (Object->TryGetNumberField(TEXT("call_timeout_seconds"), Timeout) && FMath::IsFinite(Timeout)) { Options.CallTimeoutSeconds = FMath::Clamp(static_cast<float>(Timeout), 1.0f, 180.0f); }
            }
            if (FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Config/ToolWhitelist.json"))) && KHUMcpProtocol::ReadObject(Text, Object))
            {
                const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
                for (const FString Group : {FString(TEXT("read")), FString(TEXT("show"))})
                {
                    if (Object->TryGetArrayField(Group, Names))
                    { for (const auto& Value : *Names) { FString Name; if (Value->TryGetString(Name)) { Options.AllowedTools.Add(Name); } } }
                }
                if (Object->TryGetArrayField(TEXT("never"), Names))
                { for (const auto& Value : *Names) { FString Name; if (Value->TryGetString(Name)) { Options.NeverPatterns.Add(Name); } } }
            }
            else { UE_LOG(LogKHUTutorMcp, Warning, TEXT("Missing or invalid read/show whitelist; tool calls disabled")); }
        }
        // Fail closed even if a future config accidentally includes an unsafe tool name.
        Options.NeverPatterns.Append({TEXT("*Execute*"), TEXT("*Python*"), TEXT("*Console*"), TEXT("*Delete*"), TEXT("*Shell*")});
        Queue = MakeShared<FKHUMcpSerialQueue>(KHUCreateHttpTransport());
        AgentOptions = Options;
        Client = MakeShared<FKHUMcpClient>(Queue.ToSharedRef(), Options);
        if (Plugin.IsValid()) { AiConfig = FKHUConfig::Load(Plugin->GetBaseDir() / TEXT("Config")); }
        const TWeakPtr<FKHUMcpClient> Weak = Client;
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Mcp.List"), TEXT("List MCP schemas for permitted Read/Show tools."),
            FConsoleCommandDelegate::CreateLambda([Weak]()
            { if (auto C = Weak.Pin()) { C->ListTools([](FKHUMcpResult R) { Report(TEXT("List"), TEXT("D2_list.json"), MoveTemp(R)); }); } }), ECVF_Default));
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Mcp.Check"), TEXT("Verify this editor through asynchronous MCP Ping."),
            FConsoleCommandDelegate::CreateLambda([Weak]()
            { if (auto C = Weak.Pin()) { C->HealthCheck([](FKHUMcpResult R) { Report(TEXT("Check"), TEXT("D2_check.json"), MoveTemp(R)); }); } }), ECVF_Default));
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Mcp.Call"), TEXT("Call <allowed-full-tool-name> <JSON-object>; developer console only."),
            FConsoleCommandWithArgsDelegate::CreateLambda([Weak](const TArray<FString>& Args)
            {
                if (Args.Num() < 2) { UE_LOG(LogKHUTutorMcp, Error, TEXT("Usage: KHUTutor.Mcp.Call <name> {\"message\":\"hi\"}")); return; }
                FString Json; for (int32 Index = 1; Index < Args.Num(); ++Index) { if (!Json.IsEmpty()) { Json += TEXT(" "); } Json += Args[Index]; }
                if (auto C = Weak.Pin()) { C->CallTool(Args[0], Json, [](FKHUMcpResult R) { Report(TEXT("Call"), TEXT("D2_call.json"), MoveTemp(R)); }); }
            }), ECVF_Default));
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Ask"), TEXT("Ask <question> [--mock scenario_a/b/c] [--live --tier <name>] [--direct]. Default is Mock; Show is disabled."),
            FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
            {
                FString Question, Scenario = TEXT("scenario_b"), Tier = TEXT("T1"); bool Live = false, Direct = false;
                for (int32 I = 0; I < Args.Num(); ++I)
                {
                    if (Args[I] == TEXT("--live")) { Live = true; }
                    else if (Args[I] == TEXT("--direct")) { Direct = true; }
                    else if ((Args[I] == TEXT("--mock") || Args[I] == TEXT("--tier")) && Args.IsValidIndex(I + 1))
                    { const bool Mock = Args[I] == TEXT("--mock"); const FString V = Args[++I]; if (Mock) { Scenario = V; Live = false; } else { Tier = V; } }
                    else { if (!Question.IsEmpty()) { Question += TEXT(" "); } Question += Args[I]; }
                }
                if (!AskTutor(Question, Scenario, Live, Tier, Direct)) { UE_LOG(LogKHUTutorMcp, Display, TEXT("질문 또는 설정을 확인하세요. 진행 중인 작업은 Cancel로 취소할 수 있습니다.")); }
            }), ECVF_Default));
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Cancel"), TEXT("Cancel this tutor task without affecting other MCP clients."),
            FConsoleCommandDelegate::CreateLambda([this]() { if (Loop) { Loop->Cancel(); } }), ECVF_Default));
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Continue"), TEXT("Continue a paused tutor task."),
            FConsoleCommandDelegate::CreateLambda([this]() { if (Loop) { Loop->Resume(); } }), ECVF_Default));
        for (const auto& Warning : AiConfig.Warnings) { UE_LOG(LogKHUTutorMcp, Display, TEXT("KHUTutor config: %s"), *Warning); }
        UI = MakeUnique<FKHUTutorUI>(); UI->Register();
        UE_LOG(LogKHUTutorMcp, Display, TEXT("Tutor panels use local editor tools. Optional development MCP client ready. No HTTP or paid request is sent during module startup."));
    }

    virtual TSharedRef<FKHUMcpClient> GetMcpClient() override { return Client.ToSharedRef(); }
    virtual TSharedPtr<FKHUAgentLoop> GetAgentLoop() override { return Loop; }
    virtual TSharedRef<IKHUToolExecutor> CreatePanelTools() override
    {
        return MakeShared<FKHUToolExecutorDirect>();
    }
    virtual FKHUConfig PanelConfig() override
    {
        auto C = FKHUConfig::Load(IPluginManager::Get().FindPlugin(TEXT("KHUTutor"))->GetBaseDir() / TEXT("Config"));
        const FString Url = GetDefault<UKHUTutorSettings>()->BaseUrl.TrimStartAndEnd();
        if (!Url.IsEmpty())
        {
            if (!Url.StartsWith(TEXT("https://")) || Url.Contains(TEXT("@")) || Url.Contains(TEXT("?")) || Url.Contains(TEXT("#"))) { C.bValid = false; }
            else { C.BaseUrl = Url; C.BaseUrl.RemoveFromEnd(TEXT("/")); }
        }
        const FString Exe = GetDefault<UKHUTutorSettings>()->ClaudeExecutable.TrimStartAndEnd();
        if (!Exe.IsEmpty()) { C.ClaudeExecutable = Exe; }
        return C;
    }
    virtual TSharedPtr<FKHUAgentLoop> CreatePanelLoop(bool bMock, FString& Error) override
    {
        const auto C = PanelConfig(); TSharedPtr<IKHUChatClient> Chat;
        if (bMock) { Chat = MakeShared<FKHUPanelMock>(); }
        else
        {
            FString Key;
            if (!C.bValid) { Error = TEXT("설정 파일 또는 ChatKHU 주소를 확인하세요."); return nullptr; }
            if (!FKHUSecretStore(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi")).Load(Key, Error)) { return nullptr; }
            if (C.bClaudePanel)
            {
                if (FKHUClaudeRuntime::Executable(C).IsEmpty()) { Error = TEXT("Claude 연결 프로그램이 준비되지 않았습니다. 고급 설정에서 claude.exe 위치를 확인해 주세요."); return nullptr; }
                Chat = MakeShared<FKHUClaudeRuntime>(C, MoveTemp(Key));
            }
            else { Chat = MakeShared<FKHUChatClient>(C, MoveTemp(Key), KHUCreateHttpTransport()); }
        }
        return MakeShared<FKHUAgentLoop>(C, Chat.ToSharedRef(), CreatePanelTools(),
            MakeShared<FKHUToolBridge>(C, FKHUToolExecutorDirect::Catalog()), FPaths::ProjectSavedDir() / TEXT("KHUTutor/calib.json"));
    }
    virtual bool AskTutor(const FString& Question, const FString& Scenario, bool bLive, const FString& Tier, bool bDirect) override
    {
        if (Loop && Loop->State() != EKHUAgentState::Idle) { return false; }
        TSharedPtr<IKHUChatClient> Chat;
        if (bLive)
        {
            FString Key, Error; const FKHUSecretStore Store(FPaths::ProjectSavedDir() / TEXT("KHUTutor/api_key.dpapi"));
            if (!AiConfig.bValid || !Store.Load(Key, Error)) { UE_LOG(LogKHUTutorMcp, Display, TEXT("%s"), *Error); return false; }
            Chat = MakeShared<FKHUChatClient>(AiConfig, MoveTemp(Key), KHUCreateHttpTransport());
        }
        else
        {
            if (Scenario != TEXT("scenario_a") && Scenario != TEXT("scenario_b") && Scenario != TEXT("scenario_c")) { return false; }
            Chat = FKHUMockChatClient::Load(AiConfig.FixtureDirectory / (Scenario + TEXT(".json")));
        }
        TSharedPtr<IKHUToolExecutor> Executor;
        if (bDirect) { Executor = MakeShared<FKHUToolExecutorDirect>(); }
        else { Executor = MakeShared<FKHUToolExecutorMcp>(Queue.ToSharedRef(), AgentOptions); }
        auto Bridge = MakeShared<FKHUToolBridge>(AiConfig, FKHUToolExecutorDirect::Catalog());
        Loop = MakeShared<FKHUAgentLoop>(AiConfig, Chat.ToSharedRef(), Executor.ToSharedRef(), Bridge, FPaths::ProjectSavedDir() / TEXT("KHUTutor/calib.json"));
        const TWeakPtr<FKHUAgentLoop> WeakLoop = Loop;
        return Loop->Ask(Question, Tier, 1, [WeakLoop](EKHUAgentState State, const FString& Text)
        {
            UE_LOG(LogKHUTutorMcp, Display, TEXT("Tutor: %s"), *Text);
            if (State == EKHUAgentState::Idle || State == EKHUAgentState::AskContinue)
            {
                if (auto Agent = WeakLoop.Pin())
                {
                    const FString Dir = FPaths::ProjectSavedDir() / TEXT("KHUTutor/Console"); IFileManager::Get().MakeDirectory(*Dir, true);
                    FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Agent->Receipt()), *(Dir / TEXT("last_run.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                }
            }
        }, bLive, false);
    }

    virtual void ShutdownModule() override
    {
        if (UI) { UI->Unregister(); UI.Reset(); }
        for (IConsoleCommand* Command : Commands) { IConsoleManager::Get().UnregisterConsoleObject(Command, false); }
        Commands.Reset();
        if (Loop) { Loop->Cancel(); Loop.Reset(); }
        if (Client.IsValid()) { Client->Shutdown(); Client.Reset(); }
        if (Queue.IsValid()) { Queue->Shutdown(); Queue.Reset(); }
    }
private:
    TSharedPtr<FKHUMcpSerialQueue> Queue;
    TSharedPtr<FKHUMcpClient> Client;
    TArray<IConsoleCommand*> Commands;
    FKHUConfig AiConfig;
    FKHUMcpOptions AgentOptions;
    TSharedPtr<FKHUAgentLoop> Loop;
    TUniquePtr<FKHUTutorUI> UI;
};

IMPLEMENT_MODULE(FKHUTutorModule, KHUTutor)

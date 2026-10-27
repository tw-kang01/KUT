#pragma once
#include "AI/KHUChatClient.h"
#include "AI/KHUProjectMemory.h"

namespace KHUClaudeProtocol
{
    /** Quote exactly one Windows argv element, including trailing backslashes. */
    KHUTUTOR_API FString Quote(const FString& Argument);
    /** Parse one run's usage, never cumulative modelUsage/total_cost_usd. Hint prevents joining visible continuations at L0. */
    KHUTUTOR_API FKHUChatResponse Result(const TSharedRef<FJsonObject>& Event, const FKHUConfig& Config, const FString& Model,
        const TArray<TSharedPtr<FJsonValue>>& Stream = {}, int32 Hint = 0);
    /** Validate the connected server and exact exposed tool set from CLI initialization. */
    KHUTUTOR_API TSharedRef<FJsonObject> RuntimeInfo(const TSharedPtr<FJsonObject>& Init, const FString& SessionId, bool bResumed, const TArray<TSharedPtr<FJsonValue>>& ExpectedSchemas);
    /** Everything a managed CLI session must still match to be resumed: project, asset, folder, model, hint, product instructions, coaching boundary. */
    KHUTUTOR_API TSharedRef<FJsonObject> SessionIdentity(const FString& Project, const FKHUChatRequest& Request, const FString& Folder, const FString& SystemPrompt);
    /** Resume only a usable stored session whose identity is unchanged; otherwise the caller starts a fresh session. */
    KHUTUTOR_API bool CanResume(const TSharedPtr<FJsonObject>& Previous, const TSharedRef<FJsonObject>& Identity, FString& SessionId);
    /** Split a request for the CLI: product instructions and the hint note stay in the system file (identical between turns, so the
     *  prompt cache is read instead of rewritten); application state notes travel with the student message. */
    KHUTUTOR_API void Compose(const FKHUChatRequest& Request, FString& OutSystem, FString& OutInput);
    /** Search/read only the selected learning folder and exact asset, with hint filtering. */
    KHUTUTOR_API FString History(FKHUProjectMemory& Memory, const FKHUChatRequest& Request, const FString& Tool, const TSharedRef<FJsonObject>& Arguments);
}

/** Owned native process + private loopback MCP; no Python, shell or global environment changes. */
class KHUTUTOR_API FKHUClaudeRuntime final : public IKHUChatClient, public TSharedFromThis<FKHUClaudeRuntime>
{
public:
    /** Use the supplied project archive, or the panel's default archive when omitted. */
    FKHUClaudeRuntime(FKHUConfig InConfig, FString InKey, TSharedPtr<FKHUProjectMemory> InProjectMemory = nullptr);
    virtual ~FKHUClaudeRuntime() override;
    virtual void Send(FKHUChatRequest Request, FCallback Callback) override;
    virtual void Cancel() override;
    virtual bool IsMock() const override { return false; }
    virtual bool OwnsHistory() const override { return true; }
    virtual void ObserveTools(FToolObserver Observer) override { ToolObserver = MoveTemp(Observer); }
    /** Resolve the shipped/project-private binary without consulting PATH or launching a shell. */
    static FString Executable(const FKHUConfig& Config);
    /** Development-only subscription token variable; honored only when DevelopmentBudget authorizes unlimited development. */
    static const TCHAR* SubscriptionTokenVariable() { return TEXT("KHU_DEV_CLAUDE_OAUTH_TOKEN"); }
    /** First CLAUDE.md the CLI would auto-discover from Directory or its parents (non-bare mode), or false when none. */
    static bool FindInstructionFile(const FString& Directory, FString& OutFound);
private:
    struct FRun;
    void Finished(uint64 Serial, FKHUChatResponse Response, TSharedPtr<FJsonObject> Session);
    FKHUConfig Config;
    FString Key;
    TSharedPtr<FKHUProjectMemory> ProjectMemory;
    TSharedPtr<FRun, ESPMode::ThreadSafe> Run;
    FCallback Completion;
    FToolObserver ToolObserver;
    uint64 Generation = 0;
};

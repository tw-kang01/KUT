#pragma once
#include "AI/KHUConfig.h"
#include "Mcp/KHUMcpTransport.h"
#include "Containers/Ticker.h"

struct KHUTUTOR_API FKHUChatRequest
{
    FString Model, SerializedTools;
    TArray<TSharedPtr<FJsonValue>> Messages, Tools;
    TOptional<double> Temperature;
    FString ReasoningEffort;
    int32 MaxTokens = 0, OutputHardLimit = 0;
    int32 EstimatedInputTokens = 0, InputPayloadBytes = 0, OmittedTurns = 0;
    double RemainingCredits = 0, AttemptCreditReserve = 0;
    bool bUnlimitedDevelopment = false;
    bool bResponses = false;
    FString TaskId, AssetScope, Tier;
    FString CoachControl;
    int32 Hint = 1, MaxTurns = 1;
    bool bAllowShow = false;
};
struct KHUTUTOR_API FKHUToolCall { FString Id, Name, Arguments; };
struct KHUTUTOR_API FKHUChatResponse
{
    bool bSuccess = false, bMock = false, bTryNextModel = false, bBudget = false;
    FString Content, Error, FinishReason, ProviderError;
    TArray<FKHUToolCall> Calls;
    TSharedPtr<FJsonObject> Assistant;
    int64 InputTokens = 0, OutputTokens = 0, CachedTokens = 0;
    /** Tokens written to the prompt cache this run (part of InputTokens); the 1h share is a subset. */
    int64 CacheWriteTokens = 0, CacheWriteHourTokens = 0;
    bool bUsageKnown = false;
    TOptional<double> Credits;
    int32 Attempts = 0, HttpStatus = 0;
    int32 InputPayloadBytes = 0;
    bool bAggregateUsage = false, bIterationLimit = false;
    TSharedPtr<FJsonObject> Runtime;
};
/** Cancelable asynchronous Chat Completions boundary. */
class KHUTUTOR_API IKHUChatClient
{
public:
    using FCallback = TFunction<void(FKHUChatResponse)>;
    virtual ~IKHUChatClient() = default;
    virtual void Send(FKHUChatRequest Request, FCallback Callback) = 0;
    virtual void Cancel() = 0;
    virtual bool IsMock() const = 0;
    /** A managed runtime retains its own tool protocol and persisted conversation. */
    virtual bool OwnsHistory() const { return false; }
    using FToolObserver = TFunction<void(TSharedRef<FJsonObject>)>;
    virtual void ObserveTools(FToolObserver Observer) {}
};
namespace KHUChatProtocol
{
    KHUTUTOR_API FString Body(const FKHUChatRequest& Request);
    KHUTUTOR_API FKHUChatResponse Parse(const FString& Body);
    KHUTUTOR_API FString Error(int32 Status);
}
namespace KHUResponsesProtocol
{
    /** Convert bounded conversation/tool messages to stateless Responses items. */
    KHUTUTOR_API FString Body(const FKHUChatRequest& Request);
    /** Normalize text/calls/usage while preserving every reasoning and output item. */
    KHUTUTOR_API FKHUChatResponse Parse(const FString& Body);
}
/** Loads recorded responses; never owns a network transport or an API key. */
class KHUTUTOR_API FKHUMockChatClient final : public IKHUChatClient, public TSharedFromThis<FKHUMockChatClient>
{
public:
    explicit FKHUMockChatClient(TArray<FString> InResponses) : Responses(MoveTemp(InResponses)) {}
    static TSharedRef<FKHUMockChatClient> Load(const FString& Path);
    virtual void Send(FKHUChatRequest Request, FCallback Callback) override;
    virtual void Cancel() override { ++Generation; }
    virtual bool IsMock() const override { return true; }
private:
    TArray<FString> Responses;
    int32 Index = 0;
    uint64 Generation = 0;
};
/** FHttpModule implementation with first-use model discovery and bounded retries. */
class KHUTUTOR_API FKHUChatClient final : public IKHUChatClient, public TSharedFromThis<FKHUChatClient>
{
public:
    FKHUChatClient(FKHUConfig InConfig, FString InKey, TSharedRef<IKHUMcpTransport> InTransport);
    virtual ~FKHUChatClient() override;
    virtual void Send(FKHUChatRequest Request, FCallback Callback) override;
    virtual void Cancel() override;
    virtual bool IsMock() const override { return false; }
private:
    void Discover(uint64 G);
    void Post(uint64 G);
    void Received(uint64 G, FKHUMcpHttpResponse Response);
    void Retry(uint64 G, float Seconds);
    void Finish(FKHUChatResponse Response);
    FKHUConfig Config;
    FString Key;
    TSharedRef<IKHUMcpTransport> Transport;
    IKHUMcpTransport::FCancel CancelHttp;
    FTSTicker::FDelegateHandle RetryHandle;
    FCallback Completion;
    FKHUChatRequest Current;
    TSet<FString> Models, ResponseModels;
    uint64 Generation = 0, NetworkSerial = 0;
    int32 Attempts = 0, RateRetries = 0, TransientRetries = 0, LastPostedBytes = 0;
    bool bDiscovered = false, bHalved = false, bExpanded = false;
};

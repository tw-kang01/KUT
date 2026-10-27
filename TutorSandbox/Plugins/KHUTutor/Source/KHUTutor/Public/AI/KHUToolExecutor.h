#pragma once
#include "Mcp/KHUMcpClient.h"

/** Execution boundary; the loop must resolve and validate the whitelist first. */
class KHUTUTOR_API IKHUToolExecutor
{
public:
    using FCallback = TFunction<void(FKHUMcpResult)>;
    virtual ~IKHUToolExecutor() = default;
    virtual void Execute(const FString& Native, const TSharedRef<FJsonObject>& Args, FCallback Callback) = 0;
    virtual void Cancel() = 0;
};
/** Product executor invokes only owned static Read/Show methods on the game thread. */
class KHUTUTOR_API FKHUToolExecutorDirect final : public IKHUToolExecutor, public TSharedFromThis<FKHUToolExecutorDirect>
{
public:
    virtual void Execute(const FString& Native, const TSharedRef<FJsonObject>& Args, FCallback Callback) override;
    virtual void Cancel() override { ++Generation; }
    /** Obtain schemas from the actual Toolset Registry, without HTTP. */
    static TArray<TSharedPtr<FJsonValue>> Catalog();
private:
    uint64 Generation = 0;
};
/** A private MCP client shares the global FIFO but owns independent cancellation. */
class KHUTUTOR_API FKHUToolExecutorMcp final : public IKHUToolExecutor
{
public:
    FKHUToolExecutorMcp(TSharedRef<FKHUMcpSerialQueue> InQueue, FKHUMcpOptions InOptions) : Queue(InQueue), Options(MoveTemp(InOptions)) {}
    virtual void Execute(const FString& Native, const TSharedRef<FJsonObject>& Args, FCallback Callback) override;
    virtual void Cancel() override;
private:
    TSharedRef<FKHUMcpSerialQueue> Queue;
    FKHUMcpOptions Options;
    TSharedPtr<FKHUMcpClient> Client;
};

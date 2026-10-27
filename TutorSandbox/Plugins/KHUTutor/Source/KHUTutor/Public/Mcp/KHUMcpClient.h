#pragma once

#include "Mcp/KHUMcpProtocol.h"
#include "Mcp/KHUMcpTransport.h"

/** Local endpoint, bounded request timeouts, and explicit read-only tool permissions. */
struct KHUTUTOR_API FKHUMcpOptions
{
    FString Url = TEXT("http://127.0.0.1:8001/mcp");
    FString ExpectedProject;
    float ControlTimeoutSeconds = 10;
    float CallTimeoutSeconds = 120;
    TSet<FString> AllowedTools;
    TArray<FString> NeverPatterns;
};

/** Game-thread API with asynchronous completion and serialized logical operations. */
class KHUTUTOR_API FKHUMcpClient : public TSharedFromThis<FKHUMcpClient>
{
public:
    using FCallback = TFunction<void(FKHUMcpResult)>;
    FKHUMcpClient(TSharedRef<FKHUMcpSerialQueue> InQueue, FKHUMcpOptions InOptions);
    /** Establish a session and send initialized; ready sessions are reused. */
    void Initialize(FCallback Callback);
    /** Enumerate pages and discover schemas for allowed tools in Tool Search mode. */
    void ListTools(FCallback Callback);
    /** Execute only an allowed, discovered tool, validating its argument schema first. */
    void CallTool(const FString& Name, const FString& ArgumentsJson, FCallback Callback);
    /** Check discovery and exact project identity using the read-only Ping. */
    void HealthCheck(FCallback Callback);
    /** Cancel in-flight and queued operations once, before module shutdown. */
    void Shutdown();
    static FString PingToolName();
private:
    enum class EOperation { Initialize, List, Call, Health };
    struct FOperation
    {
        EOperation Kind = EOperation::Initialize;
        FString Name;
        TSharedPtr<FJsonObject> Arguments;
        FCallback Callback;
        int32 Recoveries = 0;
    };
    void Enqueue(TSharedRef<FOperation> Operation);
    void Pump();
    void Finish(FKHUMcpResult Result);
    void EnsureInitialized(FCallback Callback);
    void SendRpc(const FString& Method, TSharedRef<FJsonObject> Params, bool bNotification, float Timeout, FCallback Callback);
    void ReadTools(FCallback Callback);
    void ReadPage(const FString& Cursor, TSharedRef<FCallback> Callback);
    void Discover(TSharedRef<FCallback> Callback);
    void DescribeNext(int32 Index, TSharedRef<FCallback> Callback);
    FKHUMcpResult ToolCatalog() const;
    void InvokeTool(const FString& Name, TSharedRef<FJsonObject> Arguments, FCallback Callback);
    bool IsAllowed(const FString& Name) const;
    bool ValidateArguments(const FString& Name, const TSharedRef<FJsonObject>& Arguments) const;
    TSharedRef<FKHUMcpSerialQueue> Queue;
    FKHUMcpOptions Options;
    uint64 Owner = 0;
    int32 NextRpcId = 0;
    FString SessionId;
    FString ProtocolVersion;
    TSharedPtr<FJsonObject> InitializeResult;
    TArray<TSharedPtr<FOperation>> Pending;
    TSharedPtr<FOperation> Active;
    TArray<TSharedPtr<FJsonValue>> NativeTools;
    TMap<FString, TSharedPtr<FJsonObject>> Schemas;
    TArray<FString> Toolsets;
    TSet<FString> SeenCursors;
    int32 PageCount = 0;
    bool bReady = false;
    bool bProjectVerified = false;
    bool bHaveTools = false;
    bool bSearchMode = false;
    bool bStopped = false;
    bool bPumping = false;
    bool bDelivering = false;
};

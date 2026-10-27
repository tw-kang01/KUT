#pragma once

#include "CoreMinimal.h"

/** An HTTP request independent of the engine HTTP implementation. */
struct KHUTUTOR_API FKHUMcpHttpRequest
{
    FString Url;
    FString Verb = TEXT("POST");
    FString Body;
    TMap<FString, FString> Headers;
    float TimeoutSeconds = 10.0f;
};

/** A bounded HTTP response; transport failure is distinct from an HTTP error. */
struct KHUTUTOR_API FKHUMcpHttpResponse
{
    int32 Status = 0;
    FString Body;
    TMap<FString, FString> Headers;
    FString Error;
    FString Header(const FString& Name) const;
};

/** Send asynchronously and complete on the game thread. Cancellation must stop callbacks. */
class KHUTUTOR_API IKHUMcpTransport
{
public:
    using FCompletion = TFunction<void(FKHUMcpHttpResponse)>;
    using FCancel = TFunction<void()>;
    virtual ~IKHUMcpTransport() = default;
    virtual FCancel Send(const FKHUMcpHttpRequest& Request, FCompletion Completion) = 0;
};

/** Create the FHttpModule transport. No synchronous HTTP API is used. */
KHUTUTOR_API TSharedRef<IKHUMcpTransport> KHUCreateHttpTransport();

/** Shared FIFO for all clients in this plugin; at most one HTTP request is active. */
class KHUTUTOR_API FKHUMcpSerialQueue : public TSharedFromThis<FKHUMcpSerialQueue>
{
public:
    explicit FKHUMcpSerialQueue(TSharedRef<IKHUMcpTransport> InTransport);
    /** Queue a request belonging to a client, including initialization requests. */
    void Enqueue(uint64 Owner, FKHUMcpHttpRequest Request, IKHUMcpTransport::FCompletion Completion);
    /** Cancel one owner's requests, discarding late responses without affecting other owners. */
    void CancelOwner(uint64 Owner);
    /** Reject future work and cancel every request before module unload. */
    void Shutdown();
private:
    struct FEntry
    {
        uint64 Id = 0;
        uint64 Owner = 0;
        FKHUMcpHttpRequest Request;
        IKHUMcpTransport::FCompletion Completion;
    };
    void Pump();
    void Complete(uint64 Id, FKHUMcpHttpResponse Response);
    void CancelMatching(uint64 Owner, bool bAll);
    TSharedRef<IKHUMcpTransport> Transport;
    TArray<FEntry> Pending;
    TOptional<FEntry> Active;
    IKHUMcpTransport::FCancel CancelActive;
    uint64 NextId = 0;
    bool bPumping = false;
    bool bDelivering = false;
    bool bStopped = false;
};

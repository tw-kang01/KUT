#include "Mcp/KHUMcpTransport.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Containers/StringConv.h"

FString FKHUMcpHttpResponse::Header(const FString& Name) const
{
    for (const auto& Pair : Headers) { if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase)) { return Pair.Value; } }
    return FString();
}

class FKHUHttpTransport final : public IKHUMcpTransport
{
public:
    virtual FCancel Send(const FKHUMcpHttpRequest& Input, FCompletion Completion) override
    {
        check(IsInGameThread());
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Input.Url);
        Request->SetVerb(Input.Verb);
        Request->SetTimeout(Input.TimeoutSeconds);
        Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
        for (const auto& Header : Input.Headers) { Request->SetHeader(Header.Key, Header.Value); }
        const FTCHARToUTF8 Utf8(*Input.Body);
        TArray<uint8> Payload;
        Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        Request->SetContent(MoveTemp(Payload));
        auto Once = MakeShared<FCompletion>(MoveTemp(Completion));
        Request->OnProcessRequestComplete().BindLambda([Once](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
        {
            if (!*Once) { return; }
            FKHUMcpHttpResponse Output;
            if (!bConnected || !Response.IsValid()) { Output.Error = TEXT("MCP HTTP connection failed or timed out"); }
            else if (Response->GetContentLength() > 8 * 1024 * 1024) { Output.Error = TEXT("MCP HTTP response exceeds the size limit"); }
            else
            {
                Output.Status = Response->GetResponseCode();
                Output.Body = Response->GetContentAsString();
                for (const FString& Header : Response->GetAllHeaders())
                {
                    FString Name, Value;
                    if (Header.Split(TEXT(":"), &Name, &Value)) { Output.Headers.Add(Name.TrimStartAndEnd(), Value.TrimStartAndEnd()); }
                }
            }
            FCompletion Callback = MoveTemp(*Once);
            Callback(MoveTemp(Output));
        });
        if (!Request->ProcessRequest() && *Once)
        {
            Request->OnProcessRequestComplete().Unbind();
            FCompletion Callback = MoveTemp(*Once);
            FKHUMcpHttpResponse Output;
            Output.Error = TEXT("MCP HTTP request could not start");
            Callback(MoveTemp(Output));
        }
        return [Request, Once]() { *Once = FCompletion(); Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); };
    }
};

TSharedRef<IKHUMcpTransport> KHUCreateHttpTransport() { return MakeShared<FKHUHttpTransport>(); }

FKHUMcpSerialQueue::FKHUMcpSerialQueue(TSharedRef<IKHUMcpTransport> InTransport) : Transport(InTransport) {}

void FKHUMcpSerialQueue::Enqueue(uint64 Owner, FKHUMcpHttpRequest Request, IKHUMcpTransport::FCompletion Completion)
{
    check(IsInGameThread());
    if (bStopped || Pending.Num() >= 128)
    {
        FKHUMcpHttpResponse Response;
        Response.Error = bStopped ? TEXT("MCP queue stopped") : TEXT("MCP queue full");
        Completion(MoveTemp(Response));
        return;
    }
    FEntry Entry;
    Entry.Id = ++NextId;
    Entry.Owner = Owner;
    Entry.Request = MoveTemp(Request);
    Entry.Completion = MoveTemp(Completion);
    Pending.Add(MoveTemp(Entry));
    Pump();
}

void FKHUMcpSerialQueue::Pump()
{
    if (bPumping || bDelivering || bStopped) { return; }
    bPumping = true;
    while (!Active.IsSet() && !Pending.IsEmpty() && !bStopped)
    {
        Active.Emplace(MoveTemp(Pending[0]));
        Pending.RemoveAt(0);
        const uint64 Id = Active->Id;
        const FKHUMcpHttpRequest Request = Active->Request;
        const TWeakPtr<FKHUMcpSerialQueue> Weak = AsShared();
        IKHUMcpTransport::FCancel Cancel = Transport->Send(Request, [Weak, Id](FKHUMcpHttpResponse Response)
        {
            if (const auto Queue = Weak.Pin()) { Queue->Complete(Id, MoveTemp(Response)); }
        });
        if (Active.IsSet() && Active->Id == Id) { CancelActive = MoveTemp(Cancel); }
    }
    bPumping = false;
}

void FKHUMcpSerialQueue::Complete(uint64 Id, FKHUMcpHttpResponse Response)
{
    check(IsInGameThread());
    if (!Active.IsSet() || Active->Id != Id) { return; }
    auto Callback = MoveTemp(Active->Completion);
    Active.Reset();
    CancelActive = IKHUMcpTransport::FCancel();
    bDelivering = true;
    Callback(MoveTemp(Response));
    bDelivering = false;
    Pump();
}

void FKHUMcpSerialQueue::CancelMatching(uint64 Owner, bool bAll)
{
    TArray<IKHUMcpTransport::FCompletion> Callbacks;
    for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
    {
        if (bAll || Pending[Index].Owner == Owner) { Callbacks.Add(MoveTemp(Pending[Index].Completion)); Pending.RemoveAt(Index); }
    }
    if (Active.IsSet() && (bAll || Active->Owner == Owner))
    {
        Callbacks.Add(MoveTemp(Active->Completion));
        Active.Reset();
        auto Cancel = MoveTemp(CancelActive);
        if (Cancel) { Cancel(); }
    }
    const bool bWasDelivering = bDelivering;
    bDelivering = true;
    for (auto& Callback : Callbacks)
    {
        FKHUMcpHttpResponse Response;
        Response.Error = TEXT("MCP request cancelled");
        Callback(MoveTemp(Response));
    }
    bDelivering = bWasDelivering;
    Pump();
}

void FKHUMcpSerialQueue::CancelOwner(uint64 Owner) { check(IsInGameThread()); CancelMatching(Owner, false); }
void FKHUMcpSerialQueue::Shutdown() { check(IsInGameThread()); bStopped = true; CancelMatching(0, true); }

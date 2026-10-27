#include "Mcp/KHUMcpClient.h"
#include "Dom/JsonValue.h"

namespace
{
    uint64 NextOwner = 0; // Every client and queue operation is confined to the game thread.
    TSharedRef<FJsonObject> EmptyObject() { return MakeShared<FJsonObject>(); }
    TSharedPtr<FJsonValue> ExactField(const TSharedRef<FJsonObject>& Object, const FString& Name)
    {
        // UE JSON lookups use case-insensitive keys; JSON Schema property names do not.
        for (const auto& Field : Object->Values)
        { if (FString(*Field.Key).Equals(Name, ESearchCase::CaseSensitive)) { return Field.Value; } }
        return nullptr;
    }
}

FString FKHUMcpClient::PingToolName() { return TEXT("KHUTutorTools.KHUTutorReadToolset.Ping"); }

FKHUMcpClient::FKHUMcpClient(TSharedRef<FKHUMcpSerialQueue> InQueue, FKHUMcpOptions InOptions)
    : Queue(InQueue), Options(MoveTemp(InOptions)), Owner(++NextOwner) { check(IsInGameThread()); }

void FKHUMcpClient::Initialize(FCallback Callback)
{
    auto Op = MakeShared<FOperation>(); Op->Callback = MoveTemp(Callback); Enqueue(Op);
}

void FKHUMcpClient::ListTools(FCallback Callback)
{
    auto Op = MakeShared<FOperation>(); Op->Kind = EOperation::List; Op->Callback = MoveTemp(Callback); Enqueue(Op);
}

void FKHUMcpClient::HealthCheck(FCallback Callback)
{
    auto Op = MakeShared<FOperation>(); Op->Kind = EOperation::Health; Op->Callback = MoveTemp(Callback); Enqueue(Op);
}

void FKHUMcpClient::CallTool(const FString& Name, const FString& ArgumentsJson, FCallback Callback)
{
    check(IsInGameThread());
    TSharedPtr<FJsonObject> Arguments;
    if (!IsAllowed(Name)) { Callback(FKHUMcpResult::Failure(TEXT("Tool is not in the Read/Show whitelist"))); return; }
    if (ArgumentsJson.Len() > 64000 || !KHUMcpProtocol::ReadObject(ArgumentsJson, Arguments))
    { Callback(FKHUMcpResult::Failure(TEXT("Tool arguments must be a bounded JSON object"))); return; }
    auto Op = MakeShared<FOperation>(); Op->Kind = EOperation::Call; Op->Name = Name;
    Op->Arguments = Arguments; Op->Callback = MoveTemp(Callback); Enqueue(Op);
}

bool FKHUMcpClient::IsAllowed(const FString& Name) const
{
    if (!Options.AllowedTools.Contains(Name)) { return false; }
    for (const FString& Pattern : Options.NeverPatterns) { if (Name.MatchesWildcard(Pattern)) { return false; } }
    return true;
}

void FKHUMcpClient::Enqueue(TSharedRef<FOperation> Operation)
{
    check(IsInGameThread());
    if (bStopped || !KHUMcpProtocol::IsSafeLocalUrl(Options.Url) || Pending.Num() >= 64)
    { Operation->Callback(FKHUMcpResult::Failure(TEXT("MCP client stopped, endpoint unsafe, or operation queue full"))); return; }
    Pending.Add(Operation); Pump();
}

void FKHUMcpClient::Pump()
{
    if (bStopped || bPumping || bDelivering) { return; }
    bPumping = true;
    while (!Active.IsValid() && !Pending.IsEmpty() && !bStopped)
    {
        Active = Pending[0]; Pending.RemoveAt(0);
        const TWeakPtr<FKHUMcpClient> Weak = AsShared();
        EnsureInitialized([Weak](FKHUMcpResult Result)
        {
            const auto Self = Weak.Pin();
            if (!Self.IsValid() || Self->bStopped || !Self->Active.IsValid()) { return; }
            if (!Result.bSuccess || Self->Active->Kind == EOperation::Initialize) { Self->Finish(MoveTemp(Result)); return; }
            const bool bCall = Self->Active->Kind == EOperation::Call;
            if (bCall && Self->bHaveTools)
            {
                Self->InvokeTool(Self->Active->Name, Self->Active->Arguments.ToSharedRef(), [Weak](FKHUMcpResult R)
                { if (auto C = Weak.Pin()) { C->Finish(MoveTemp(R)); } });
                return;
            }
            Self->ReadTools([Weak](FKHUMcpResult Catalog)
            {
                auto Client = Weak.Pin();
                if (!Client.IsValid() || Client->bStopped || !Client->Active.IsValid()) { return; }
                if (!Catalog.bSuccess || Client->Active->Kind == EOperation::List) { Client->Finish(MoveTemp(Catalog)); return; }
                FString Name = Client->Active->Name;
                TSharedRef<FJsonObject> Args = Client->Active->Arguments.IsValid() ? Client->Active->Arguments.ToSharedRef() : EmptyObject();
                if (Client->Active->Kind == EOperation::Health) { Name = PingToolName(); Args->SetStringField(TEXT("message"), TEXT("KHU health check")); }
                Client->InvokeTool(Name, Args, [Weak](FKHUMcpResult R)
                { if (auto C = Weak.Pin()) { C->Finish(MoveTemp(R)); } });
            });
        });
    }
    bPumping = false;
}

void FKHUMcpClient::Finish(FKHUMcpResult Result)
{
    if (bStopped || !Active.IsValid()) { return; }
    auto Callback = MoveTemp(Active->Callback);
    Active.Reset();
    bDelivering = true; Callback(MoveTemp(Result)); bDelivering = false; Pump();
}

void FKHUMcpClient::EnsureInitialized(FCallback Callback)
{
    if (bReady)
    {
        FKHUMcpResult Result; Result.bSuccess = true; Result.Object = InitializeResult; Callback(MoveTemp(Result)); return;
    }
    auto Params = EmptyObject();
    Params->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
    Params->SetObjectField(TEXT("capabilities"), EmptyObject());
    auto Info = EmptyObject(); Info->SetStringField(TEXT("name"), TEXT("KHUTutor")); Info->SetStringField(TEXT("version"), TEXT("0.2.0"));
    Params->SetObjectField(TEXT("clientInfo"), Info);
    auto Done = MakeShared<FCallback>(MoveTemp(Callback));
    const TWeakPtr<FKHUMcpClient> Weak = AsShared();
    SendRpc(TEXT("initialize"), Params, false, Options.ControlTimeoutSeconds, [Weak, Done](FKHUMcpResult Result)
    {
        auto Self = Weak.Pin(); if (!Self.IsValid() || Self->bStopped) { return; }
        if (!Result.bSuccess) { (*Done)(MoveTemp(Result)); return; }
        FString Version;
        if (Self->SessionId.IsEmpty() || !Result.Object->TryGetStringField(TEXT("protocolVersion"), Version)
            || (Version != TEXT("2025-06-18") && Version != TEXT("2025-11-25") && Version != TEXT("2024-11-05")))
        { Self->SessionId.Reset(); (*Done)(FKHUMcpResult::Failure(TEXT("Missing session or unsupported MCP protocol"))); return; }
        Self->ProtocolVersion = Version; Self->InitializeResult = Result.Object;
        Self->SendRpc(TEXT("notifications/initialized"), EmptyObject(), true, Self->Options.ControlTimeoutSeconds, [Weak, Done](FKHUMcpResult Notified)
        {
            auto Client = Weak.Pin(); if (!Client.IsValid() || Client->bStopped) { return; }
            Client->bReady = Notified.bSuccess;
            if (Notified.bSuccess) { Notified.Object = Client->InitializeResult; }
            (*Done)(MoveTemp(Notified));
        });
    });
}

void FKHUMcpClient::SendRpc(const FString& Method, TSharedRef<FJsonObject> Params, bool bNotification, float Timeout, FCallback Callback)
{
    if (bStopped || NextRpcId == MAX_int32) { Callback(FKHUMcpResult::Failure(TEXT("MCP client stopped or request ID limit reached"))); return; }
    const int32 Id = bNotification ? 0 : ++NextRpcId;
    auto Body = EmptyObject(); Body->SetStringField(TEXT("jsonrpc"), TEXT("2.0")); Body->SetStringField(TEXT("method"), Method);
    Body->SetObjectField(TEXT("params"), Params); if (!bNotification) { Body->SetNumberField(TEXT("id"), Id); }
    FKHUMcpHttpRequest Request; Request.Url = Options.Url; Request.Body = KHUMcpProtocol::WriteObject(Body); Request.TimeoutSeconds = Timeout;
    Request.Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
    Request.Headers.Add(TEXT("Accept"), TEXT("application/json, text/event-stream"));
    Request.Headers.Add(TEXT("User-Agent"), TEXT("KHU-UE-Tutor/0.2"));
    if (Method != TEXT("initialize") && !SessionId.IsEmpty())
    { Request.Headers.Add(TEXT("Mcp-Session-Id"), SessionId); Request.Headers.Add(TEXT("MCP-Protocol-Version"), ProtocolVersion); }
    const TWeakPtr<FKHUMcpClient> Weak = AsShared();
    auto Done = MakeShared<FCallback>(MoveTemp(Callback));
    Queue->Enqueue(Owner, MoveTemp(Request), [Weak, Method, Params, bNotification, Timeout, Id, Done](FKHUMcpHttpResponse Response)
    {
        auto Self = Weak.Pin(); if (!Self.IsValid() || Self->bStopped) { return; }
        // Only explicit, pre-execution session errors may replay. Never replay a timeout or tool failure.
        const bool bSessionError = (Response.Status == 404 && Response.Body.Contains(TEXT("Unknown session id")))
            || (Response.Status == 400 && Response.Body.Contains(TEXT("Missing required Mcp-Session-Id")));
        if (bSessionError && Method != TEXT("initialize") && !Self->SessionId.IsEmpty() && Self->Active.IsValid() && Self->Active->Recoveries == 0)
        {
            ++Self->Active->Recoveries;
            FString BusinessName;
            if (Method == TEXT("tools/call"))
            {
                Params->TryGetStringField(TEXT("name"), BusinessName);
                if (BusinessName == TEXT("call_tool"))
                {
                    const TSharedPtr<FJsonObject>* WrappedArgs = nullptr;
                    FString Toolset, Function;
                    if (Params->TryGetObjectField(TEXT("arguments"), WrappedArgs))
                    {
                        (*WrappedArgs)->TryGetStringField(TEXT("toolset_name"), Toolset);
                        (*WrappedArgs)->TryGetStringField(TEXT("tool_name"), Function);
                        BusinessName = Toolset + TEXT(".") + Function;
                    }
                }
            }
            const bool bVerifyBeforeReplay = Self->IsAllowed(BusinessName) && BusinessName != PingToolName();
            Self->bReady = false; Self->bHaveTools = false; Self->bProjectVerified = false; Self->SessionId.Reset(); Self->ProtocolVersion.Reset();
            Self->EnsureInitialized([Weak, Method, Params, bNotification, Timeout, Done, bVerifyBeforeReplay](FKHUMcpResult Initialized)
            {
                auto Client = Weak.Pin(); if (!Client.IsValid() || Client->bStopped) { return; }
                if (!Initialized.bSuccess) { (*Done)(MoveTemp(Initialized)); return; }
                if (bVerifyBeforeReplay)
                {
                    auto PingArgs = EmptyObject(); PingArgs->SetStringField(TEXT("message"), TEXT("KHU project verification after session recovery"));
                    Client->InvokeTool(PingToolName(), PingArgs, [Weak, Method, Params, bNotification, Timeout, Done](FKHUMcpResult Ping)
                    {
                        auto C = Weak.Pin(); if (!C.IsValid() || C->bStopped) { return; }
                        if (!Ping.bSuccess) { (*Done)(MoveTemp(Ping)); return; }
                        C->SendRpc(Method, Params, bNotification, Timeout, [Done](FKHUMcpResult Retried) { (*Done)(MoveTemp(Retried)); });
                    });
                }
                else { Client->SendRpc(Method, Params, bNotification, Timeout, [Done](FKHUMcpResult Retried) { (*Done)(MoveTemp(Retried)); }); }
            });
            return;
        }
        FKHUMcpResult Result;
        if (!Response.Error.IsEmpty()) { Result = FKHUMcpResult::Failure(Response.Error); }
        else if (Response.Status < 200 || Response.Status >= 300) { Result = FKHUMcpResult::Failure(FString::Printf(TEXT("MCP HTTP %d"), Response.Status)); }
        else if (bNotification) { Result.bSuccess = true; Result.Object = EmptyObject(); }
        else { Result = KHUMcpProtocol::ParseResponse(Response.Body, Response.Header(TEXT("Content-Type")), Id); }
        Result.HttpStatus = Response.Status;
        if (Method == TEXT("initialize") && Result.bSuccess) { Self->SessionId = Response.Header(TEXT("Mcp-Session-Id")); }
        (*Done)(MoveTemp(Result));
    });
}

void FKHUMcpClient::ReadTools(FCallback Callback)
{
    NativeTools.Reset(); Schemas.Reset(); SeenCursors.Reset(); Toolsets.Reset(); PageCount = 0; bHaveTools = false;
    ReadPage(FString(), MakeShared<FCallback>(MoveTemp(Callback)));
}

void FKHUMcpClient::ReadPage(const FString& Cursor, TSharedRef<FCallback> Callback)
{
    if (++PageCount > 100) { (*Callback)(FKHUMcpResult::Failure(TEXT("MCP pagination limit reached"))); return; }
    auto Params = EmptyObject(); if (!Cursor.IsEmpty()) { Params->SetStringField(TEXT("cursor"), Cursor); }
    const TWeakPtr<FKHUMcpClient> Weak = AsShared();
    SendRpc(TEXT("tools/list"), Params, false, Options.ControlTimeoutSeconds, [Weak, Callback](FKHUMcpResult Result)
    {
        auto Self = Weak.Pin(); if (!Self.IsValid() || Self->bStopped) { return; }
        if (!Result.bSuccess) { (*Callback)(MoveTemp(Result)); return; }
        const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
        if (!Result.Object->TryGetArrayField(TEXT("tools"), Tools) || Self->NativeTools.Num() + Tools->Num() > 2000)
        { (*Callback)(FKHUMcpResult::Failure(TEXT("Invalid or excessive MCP tool list"))); return; }
        Self->NativeTools.Append(*Tools);
        FString Next;
        Result.Object->TryGetStringField(TEXT("nextCursor"), Next);
        if (!Next.IsEmpty())
        {
            if (Self->SeenCursors.Contains(Next)) { (*Callback)(FKHUMcpResult::Failure(TEXT("Repeated MCP pagination cursor"))); return; }
            Self->SeenCursors.Add(Next); Self->ReadPage(Next, Callback); return;
        }
        Self->Discover(Callback);
    });
}

void FKHUMcpClient::Discover(TSharedRef<FCallback> Callback)
{
    TSet<FString> Names;
    for (const auto& Value : NativeTools)
    {
        FString Name;
        if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject()->TryGetStringField(TEXT("name"), Name))
        { (*Callback)(FKHUMcpResult::Failure(TEXT("Invalid tool schema in MCP catalog"))); return; }
        Names.Add(Name); if (IsAllowed(Name)) { Schemas.Add(Name, Value->AsObject()); }
    }
    bSearchMode = Names.Contains(TEXT("list_toolsets")) && Names.Contains(TEXT("describe_toolset")) && Names.Contains(TEXT("call_tool"));
    if (!bSearchMode) { bHaveTools = true; (*Callback)(ToolCatalog()); return; }
    for (const FString& Name : Options.AllowedTools)
    {
        if (!IsAllowed(Name)) { continue; }
        FString Toolset, Function;
        if (Name.Split(TEXT("."), &Toolset, &Function, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { Toolsets.AddUnique(Toolset); }
    }
    Toolsets.Sort();
    auto Params = EmptyObject(); Params->SetStringField(TEXT("name"), TEXT("list_toolsets")); Params->SetObjectField(TEXT("arguments"), EmptyObject());
    const TWeakPtr<FKHUMcpClient> Weak = AsShared();
    SendRpc(TEXT("tools/call"), Params, false, Options.CallTimeoutSeconds, [Weak, Callback](FKHUMcpResult Result)
    {
        auto Self = Weak.Pin(); if (!Self.IsValid() || Self->bStopped) { return; }
        if (!Result.bSuccess) { (*Callback)(MoveTemp(Result)); return; }
        auto Catalog = KHUMcpProtocol::ToolResult(Result.Object.ToSharedRef());
        if (!Catalog.bSuccess) { (*Callback)(MoveTemp(Catalog)); return; }
        for (const FString& Toolset : Self->Toolsets)
        { if (!Catalog.Text.Contains(Toolset)) { (*Callback)(FKHUMcpResult::Failure(TEXT("Allowed toolset is not registered in this editor"))); return; } }
        Self->DescribeNext(0, Callback);
    });
}

void FKHUMcpClient::DescribeNext(int32 Index, TSharedRef<FCallback> Callback)
{
    if (Index >= Toolsets.Num()) { bHaveTools = true; (*Callback)(ToolCatalog()); return; }
    auto Args = EmptyObject(); Args->SetStringField(TEXT("toolset_name"), Toolsets[Index]);
    auto Params = EmptyObject(); Params->SetStringField(TEXT("name"), TEXT("describe_toolset")); Params->SetObjectField(TEXT("arguments"), Args);
    const TWeakPtr<FKHUMcpClient> Weak = AsShared();
    SendRpc(TEXT("tools/call"), Params, false, Options.CallTimeoutSeconds, [Weak, Callback, Index](FKHUMcpResult Result)
    {
        auto Self = Weak.Pin(); if (!Self.IsValid() || Self->bStopped) { return; }
        if (!Result.bSuccess) { (*Callback)(MoveTemp(Result)); return; }
        auto Text = KHUMcpProtocol::ToolResult(Result.Object.ToSharedRef());
        TSharedPtr<FJsonObject> Description;
        const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
        if (!Text.bSuccess || !KHUMcpProtocol::ReadObject(Text.Text, Description) || !Description->TryGetArrayField(TEXT("tools"), Tools))
        { (*Callback)(FKHUMcpResult::Failure(TEXT("Invalid described toolset schema"))); return; }
        for (const auto& Value : *Tools)
        {
            FString Name;
            if (Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("name"), Name) && Self->IsAllowed(Name))
            { Self->Schemas.Add(Name, Value->AsObject()); }
        }
        Self->DescribeNext(Index + 1, Callback);
    });
}

FKHUMcpResult FKHUMcpClient::ToolCatalog() const
{
    FKHUMcpResult Result; Result.bSuccess = true; Result.Object = EmptyObject();
    Result.Object->SetArrayField(TEXT("tools"), NativeTools);
    TArray<TSharedPtr<FJsonValue>> Allowed;
    TArray<FString> Names; Schemas.GetKeys(Names); Names.Sort();
    for (const FString& Name : Names) { Allowed.Add(MakeShared<FJsonValueObject>(Schemas[Name])); }
    Result.Object->SetArrayField(TEXT("allowed_tools"), Allowed); Result.Object->SetBoolField(TEXT("search_mode"), bSearchMode);
    Result.Text = KHUMcpProtocol::WriteObject(Result.Object.ToSharedRef());
    return Result;
}

bool FKHUMcpClient::ValidateArguments(const FString& Name, const TSharedRef<FJsonObject>& Arguments) const
{
    const auto Schema = Schemas.Find(Name); if (!Schema) { return false; }
    const TSharedPtr<FJsonObject>* Input = nullptr;
    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!(*Schema)->TryGetObjectField(TEXT("inputSchema"), Input)) { return false; }
    FString InputType;
    if (!(*Input)->TryGetStringField(TEXT("type"), InputType) || InputType != TEXT("object")) { return false; }
    const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
    if ((*Input)->TryGetArrayField(TEXT("required"), Required))
    {
        for (const auto& Value : *Required) { FString Field; if (!Value->TryGetString(Field) || !ExactField(Arguments, Field).IsValid()) { return false; } }
    }
    // Epic omits properties for no-argument UFUNCTIONs. Only an empty object is allowed.
    if (!(*Input)->TryGetObjectField(TEXT("properties"), Properties))
    { return !(*Input)->HasField(TEXT("properties")) && Arguments->Values.IsEmpty(); }
    for (const auto& Pair : Arguments->Values)
    {
        const auto PropertyValue = ExactField(Properties->ToSharedRef(), FString(*Pair.Key));
        if (!PropertyValue.IsValid() || PropertyValue->Type != EJson::Object) { return false; }
        const auto Property = PropertyValue->AsObject();
        FString Type;
        if (!Property->TryGetStringField(TEXT("type"), Type)) { return false; }
        const EJson Actual = Pair.Value->Type;
        if ((Type == TEXT("string") && Actual != EJson::String) || (Type == TEXT("boolean") && Actual != EJson::Boolean)
            || ((Type == TEXT("number") || Type == TEXT("integer")) && Actual != EJson::Number)
            || (Type == TEXT("object") && Actual != EJson::Object) || (Type == TEXT("array") && Actual != EJson::Array)) { return false; }
    }
    return true;
}

void FKHUMcpClient::InvokeTool(const FString& Name, TSharedRef<FJsonObject> Arguments, FCallback Callback)
{
    if (!IsAllowed(Name) || !ValidateArguments(Name, Arguments))
    { Callback(FKHUMcpResult::Failure(TEXT("Tool is not allowed, not discovered, or arguments do not match its schema"))); return; }
    if (Name != PingToolName() && !bProjectVerified)
    {
        auto PingArgs = EmptyObject(); PingArgs->SetStringField(TEXT("message"), TEXT("KHU project verification"));
        const TWeakPtr<FKHUMcpClient> Weak = AsShared(); auto Done = MakeShared<FCallback>(MoveTemp(Callback));
        InvokeTool(PingToolName(), PingArgs, [Weak, Name, Arguments, Done](FKHUMcpResult Ping)
        {
            auto C = Weak.Pin(); if (!C.IsValid() || C->bStopped) { return; }
            if (!Ping.bSuccess) { (*Done)(MoveTemp(Ping)); return; }
            C->InvokeTool(Name, Arguments, [Done](FKHUMcpResult R) { (*Done)(MoveTemp(R)); });
        });
        return;
    }
    auto Params = EmptyObject();
    if (bSearchMode)
    {
        FString Toolset, Function; Name.Split(TEXT("."), &Toolset, &Function, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        auto Args = EmptyObject(); Args->SetStringField(TEXT("toolset_name"), Toolset); Args->SetStringField(TEXT("tool_name"), Function);
        Args->SetObjectField(TEXT("arguments"), Arguments); Params->SetStringField(TEXT("name"), TEXT("call_tool")); Params->SetObjectField(TEXT("arguments"), Args);
    }
    else { Params->SetStringField(TEXT("name"), Name); Params->SetObjectField(TEXT("arguments"), Arguments); }
    const TWeakPtr<FKHUMcpClient> Weak = AsShared(); auto Done = MakeShared<FCallback>(MoveTemp(Callback));
    SendRpc(TEXT("tools/call"), Params, false, Options.CallTimeoutSeconds, [Weak, Name, Done](FKHUMcpResult Result)
    {
        auto Self = Weak.Pin(); if (!Self.IsValid() || Self->bStopped) { return; }
        if (!Result.bSuccess) { (*Done)(MoveTemp(Result)); return; }
        auto Tool = KHUMcpProtocol::ToolResult(Result.Object.ToSharedRef()); Tool.HttpStatus = Result.HttpStatus;
        // Unwrap only a discovered output schema that explicitly declares a string returnValue.
        const auto* Schema = Self->Schemas.Find(Name);
        const TSharedPtr<FJsonObject>* Output = nullptr;
        const TSharedPtr<FJsonObject>* Properties = nullptr;
        const TSharedPtr<FJsonObject>* Return = nullptr;
        FString Type;
        if (Tool.bSuccess && Schema && (*Schema)->TryGetObjectField(TEXT("outputSchema"), Output)
            && (*Output)->TryGetObjectField(TEXT("properties"), Properties)
            && (*Properties)->TryGetObjectField(TEXT("returnValue"), Return)
            && (*Return)->TryGetStringField(TEXT("type"), Type) && Type == TEXT("string"))
        {
            TSharedPtr<FJsonObject> Wrapped;
            FString Text;
            if (!KHUMcpProtocol::ReadObject(Tool.Text, Wrapped) || !Wrapped->TryGetStringField(TEXT("returnValue"), Text))
            { (*Done)(FKHUMcpResult::Failure(TEXT("Tool result does not match its string return schema"))); return; }
            Tool.Text = MoveTemp(Text);
            if (Tool.Text.StartsWith(TEXT("ERROR:"))) { Tool.bSuccess = false; Tool.Error = Tool.Text; }
        }
        if (Tool.bSuccess && Name == PingToolName())
        {
            FString Identity;
            if (!KHUMcpProtocol::VerifyPingProject(Tool.Text, Self->Options.ExpectedProject, Identity))
            { (*Done)(FKHUMcpResult::Failure(TEXT("Ping returned a different editor project"))); return; }
            Tool.Text = Identity;
            Self->bProjectVerified = true;
        }
        (*Done)(MoveTemp(Tool));
    });
}

void FKHUMcpClient::Shutdown()
{
    check(IsInGameThread()); if (bStopped) { return; } bStopped = true;
    TArray<FCallback> Callbacks;
    if (Active.IsValid()) { Callbacks.Add(MoveTemp(Active->Callback)); Active.Reset(); }
    for (auto& Op : Pending) { Callbacks.Add(MoveTemp(Op->Callback)); } Pending.Reset();
    Queue->CancelOwner(Owner); bReady = false; SessionId.Reset(); ProtocolVersion.Reset();
    for (auto& Callback : Callbacks) { Callback(FKHUMcpResult::Failure(TEXT("MCP client shut down"))); }
}

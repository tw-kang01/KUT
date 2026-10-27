#include "Mcp/KHUMcpProtocol.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FKHUMcpResult FKHUMcpResult::Failure(const FString& Message)
{
    FKHUMcpResult Result;
    Result.Error = Message;
    return Result;
}

bool KHUMcpProtocol::ReadObject(const FString& Text, TSharedPtr<FJsonObject>& Object)
{
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid();
}

FString KHUMcpProtocol::WriteObject(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

FKHUMcpResult KHUMcpProtocol::ParseResponse(const FString& Body, const FString& ContentType, int32 ExpectedId)
{
    if (Body.Len() > 4 * 1024 * 1024)
    {
        return FKHUMcpResult::Failure(TEXT("MCP response exceeds the size limit"));
    }
    TArray<FString> Messages;
    if (ContentType.Contains(TEXT("text/event-stream"), ESearchCase::IgnoreCase))
    {
        FString Normalized = Body.Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n"));
        TArray<FString> Lines;
        Normalized.ParseIntoArray(Lines, TEXT("\n"), false);
        Lines.Add(TEXT(""));
        FString Data;
        for (const FString& Line : Lines)
        {
            if (Line.IsEmpty())
            {
                if (!Data.IsEmpty()) { Messages.Add(MoveTemp(Data)); Data.Reset(); }
            }
            else if (Line.StartsWith(TEXT("data:")))
            {
                FString Value = Line.Mid(5);
                if (Value.StartsWith(TEXT(" "))) { Value.RightChopInline(1); }
                if (!Data.IsEmpty()) { Data += TEXT("\n"); }
                Data += Value;
            }
        }
    }
    else if (ContentType.Contains(TEXT("application/json"), ESearchCase::IgnoreCase))
    {
        Messages.Add(Body);
    }
    else { return FKHUMcpResult::Failure(TEXT("Unsupported MCP response content type")); }

    for (const FString& Message : Messages)
    {
        TSharedPtr<FJsonObject> Rpc;
        if (!ReadObject(Message, Rpc)) { return FKHUMcpResult::Failure(TEXT("Malformed MCP JSON")); }
        FString Version;
        if (!Rpc->TryGetStringField(TEXT("jsonrpc"), Version) || Version != TEXT("2.0")) { continue; }
        const TSharedPtr<FJsonValue> Id = Rpc->TryGetField(TEXT("id"));
        double Number = 0;
        if (!Id.IsValid() || Id->Type != EJson::Number || !Id->TryGetNumber(Number) || Number != ExpectedId) { continue; }
        const TSharedPtr<FJsonObject>* Error = nullptr;
        if (Rpc->TryGetObjectField(TEXT("error"), Error))
        {
            FString Detail;
            (*Error)->TryGetStringField(TEXT("message"), Detail);
            FKHUMcpResult Result = FKHUMcpResult::Failure(Detail.IsEmpty() ? TEXT("MCP JSON-RPC error") : Detail.Left(1000));
            double Code = 0;
            if ((*Error)->TryGetNumberField(TEXT("code"), Code)) { Result.RpcErrorCode = static_cast<int32>(Code); }
            return Result;
        }
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Rpc->TryGetObjectField(TEXT("result"), Object)) { return FKHUMcpResult::Failure(TEXT("Missing MCP result object")); }
        FKHUMcpResult Result;
        Result.bSuccess = true;
        Result.Object = *Object;
        return Result;
    }
    return FKHUMcpResult::Failure(TEXT("No matching JSON-RPC response ID"));
}

FKHUMcpResult KHUMcpProtocol::ToolResult(const TSharedRef<FJsonObject>& Object)
{
    bool bError = false;
    Object->TryGetBoolField(TEXT("isError"), bError);
    if (bError) { return FKHUMcpResult::Failure(TEXT("MCP tool returned isError")); }
    TArray<FString> Texts;
    const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
    if (Object->TryGetArrayField(TEXT("content"), Content))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Content)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) { continue; }
            const TSharedPtr<FJsonObject> Item = Value->AsObject();
            FString Type, Text;
            if (Item->TryGetStringField(TEXT("type"), Type) && Type == TEXT("text") && Item->TryGetStringField(TEXT("text"), Text)) { Texts.Add(Text); }
        }
    }
    FKHUMcpResult Result;
    Result.Object = Object;
    Result.Text = FString::Join(Texts, TEXT("\n"));
    if (Texts.IsEmpty())
    {
        const TSharedPtr<FJsonObject>* Structured = nullptr;
        if (!Object->TryGetObjectField(TEXT("structuredContent"), Structured)) { return FKHUMcpResult::Failure(TEXT("No text in MCP tool result")); }
        Result.Text = WriteObject(Structured->ToSharedRef());
    }
    Result.bSuccess = true;
    return Result;
}

bool KHUMcpProtocol::VerifyPingProject(const FString& Text, const FString& ExpectedProject, FString& Identity)
{
    TSharedPtr<FJsonObject> Wrapped;
    Identity = Text;
    if (ReadObject(Text, Wrapped) && !Wrapped->TryGetStringField(TEXT("returnValue"), Identity)) { return false; }
    int32 Marker = Identity.Find(TEXT(" | project_file "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (!Identity.StartsWith(TEXT("PONG: ")) || Marker == INDEX_NONE || ExpectedProject.IsEmpty()) { return false; }
    FString Actual = FPaths::ConvertRelativePathToFull(Identity.Mid(Marker + 16).TrimStartAndEnd());
    FString Expected = FPaths::ConvertRelativePathToFull(ExpectedProject);
    FPaths::NormalizeFilename(Actual);
    FPaths::NormalizeFilename(Expected);
    return Actual.Equals(Expected, ESearchCase::IgnoreCase);
}

bool KHUMcpProtocol::IsSafeLocalUrl(const FString& Url)
{
    const FString Prefix = TEXT("http://127.0.0.1:");
    if (!Url.StartsWith(Prefix) || !Url.EndsWith(TEXT("/mcp"))) { return false; }
    const FString PortText = Url.Mid(Prefix.Len(), Url.Len() - Prefix.Len() - 4);
    if (PortText.IsEmpty() || PortText.Len() > 5) { return false; }
    for (const TCHAR Character : PortText) { if (Character < TEXT('0') || Character > TEXT('9')) { return false; } }
    const int32 Port = FCString::Atoi(*PortText);
    return Port > 0 && Port <= 65535 && Port != 8000 && Port != 8765;
}

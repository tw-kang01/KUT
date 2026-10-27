#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Result delivered exactly once for a client operation. */
struct KHUTUTOR_API FKHUMcpResult
{
    bool bSuccess = false;
    FString Error;
    TSharedPtr<FJsonObject> Object;
    FString Text;
    int32 HttpStatus = 0;
    int32 RpcErrorCode = 0;
    static FKHUMcpResult Failure(const FString& Message);
};

/** Pure protocol functions used by both the real client and offline tests. */
namespace KHUMcpProtocol
{
    KHUTUTOR_API bool ReadObject(const FString& Text, TSharedPtr<FJsonObject>& Object);
    KHUTUTOR_API FString WriteObject(const TSharedRef<FJsonObject>& Object);
    /** Accept JSON or complete SSE events; never accept a response with a different ID type. */
    KHUTUTOR_API FKHUMcpResult ParseResponse(const FString& Body, const FString& ContentType, int32 ExpectedId);
    /** Extract MCP content text without automatically unwrapping arbitrary tool JSON. */
    KHUTUTOR_API FKHUMcpResult ToolResult(const TSharedRef<FJsonObject>& Object);
    /** Validate the exact Ping-returned project path before declaring editor health. */
    KHUTUTOR_API bool VerifyPingProject(const FString& Text, const FString& ExpectedProject, FString& Identity);
    /** Permit only the local development MCP endpoint; credentials and redirects are not needed. */
    KHUTUTOR_API bool IsSafeLocalUrl(const FString& Url);
}

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Mcp/KHUMcpClient.h"
#include "AI/KHUAgentLoop.h"

/** Editor services. Product panels execute owned tools locally; MCP is for development. */
class KHUTUTOR_API IKHUTutorModule : public IModuleInterface
{
public:
    static IKHUTutorModule& Get() { return FModuleManager::LoadModuleChecked<IKHUTutorModule>(TEXT("KHUTutor")); }
    virtual TSharedRef<FKHUMcpClient> GetMcpClient() = 0;
    /** Developer entry point; paid mode must be explicitly requested. */
    virtual bool AskTutor(const FString& Question, const FString& Scenario, bool bLive, const FString& Tier, bool bDirect = false) = 0;
    virtual TSharedPtr<FKHUAgentLoop> GetAgentLoop() = 0;
    /** Create isolated, asynchronous in-process editor tools. No MCP server is required. */
    virtual TSharedRef<IKHUToolExecutor> CreatePanelTools() = 0;
    /** Load current preferences over file configuration, without sending HTTP. */
    virtual FKHUConfig PanelConfig() = 0;
    /** Build a live or deterministic mock panel loop; no request until Ask. */
    virtual TSharedPtr<FKHUAgentLoop> CreatePanelLoop(bool bMock, FString& Error) = 0;
};

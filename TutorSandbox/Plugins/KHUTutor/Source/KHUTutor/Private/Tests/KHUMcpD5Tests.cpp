#include "KHUTutorModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FD5LiveState
    {
        TMap<FString, FKHUMcpResult> Results;
        UBlueprint* Blueprint = nullptr;
        double Deadline = FPlatformTime::Seconds() + 30;
        ~FD5LiveState() { if (Blueprint) { Blueprint->RemoveFromRoot(); } }
    };
    class FD5GraphCheck final : public IAutomationLatentCommand
    {
    public:
        FD5GraphCheck(FAutomationTestBase* InTest, TSharedRef<FD5LiveState> InState) : Test(InTest), State(InState) {}
        virtual bool Update() override
        {
            if (State->Results.Num() < 6 && FPlatformTime::Seconds() < State->Deadline) { return false; }
            if (State->Results.Num() != 6) { Test->AddError(TEXT("D5 real MCP requests did not finish within 30s")); return true; }
            const FString Dir = FPaths::ProjectSavedDir() / TEXT("KHUTutor/D5"); IFileManager::Get().MakeDirectory(*Dir, true);
            for (const auto& Pair : State->Results)
            {
                Test->TestEqual(*Pair.Key, Pair.Value.bSuccess, Pair.Key == TEXT("Digest") || Pair.Key == TEXT("Errors"));
                Test->TestTrue(TEXT("D5 result bounded"), Pair.Value.Text.Len() <= 6000);
                auto Receipt = MakeShared<FJsonObject>(); Receipt->SetBoolField(TEXT("success"), Pair.Value.bSuccess);
                Receipt->SetStringField(TEXT("text"), Pair.Value.Text); Receipt->SetStringField(TEXT("error"), Pair.Value.Error);
                Receipt->SetNumberField(TEXT("http_status"), Pair.Value.HttpStatus);
                Test->TestTrue(TEXT("D5 receipt saved"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Receipt),
                    *(Dir / (Pair.Key + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            }
            Test->TestTrue(TEXT("Digest returned actual graph numbering"), State->Results[TEXT("Digest")].Text.Contains(TEXT("#1 [Custom]"))
                && State->Results[TEXT("Digest")].Text.Contains(TEXT("ERR \"Target")));
            Test->TestTrue(TEXT("Compile diagnostic shares same number"), State->Results[TEXT("Errors")].Text.Contains(TEXT("E #1")));
            Test->TestTrue(TEXT("Show is in whitelist but refuses headless GUI action"), State->Results[TEXT("Show")].Error.Contains(TEXT("graphical editor")));
            Test->TestTrue(TEXT("Invalid path reaches server validation"), State->Results[TEXT("BadPath")].HttpStatus == 200
                && State->Results[TEXT("BadPath")].Error.Contains(TEXT("loaded /Game/ Blueprint path")));
            Test->TestTrue(TEXT("Missing graph argument is blocked before HTTP"), State->Results[TEXT("MissingArgument")].HttpStatus == 0
                && !State->Results[TEXT("MissingArgument")].bSuccess);
            return true;
        }
    private:
        FAutomationTestBase* Test;
        TSharedRef<FD5LiveState> State;
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUD5LiveGraphTest, "KHUTutor.Mcp.Live.D5Graph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKHUD5LiveGraphTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorMcpSmoke"))) { AddInfo(TEXT("D5 live test skipped without -McpSmoke")); return true; }
    auto State = MakeShared<FD5LiveState>();
    const FString Name = TEXT("D5Transient_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/KHUTutorAutomation/") + Name));
    State->Blueprint = NewObject<UBlueprint>(Package, *Name, RF_Transient); State->Blueprint->AddToRoot(); State->Blueprint->ParentClass = AActor::StaticClass();
    UEdGraph* Graph = NewObject<UEdGraph>(State->Blueprint, TEXT("EventGraph")); State->Blueprint->UbergraphPages.Add(Graph);
    UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph); Node->NodeGuid = FGuid(0, 0, 0, 1); Graph->Nodes.Add(Node);
    Node->bHasCompilerMessage = true; Node->ErrorType = EMessageSeverity::Error; Node->ErrorMsg = TEXT("Target must have a connection");
    const auto Client = IKHUTutorModule::Get().GetMcpClient();
    auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("assetPath"), State->Blueprint->GetPathName());
    auto DigestArgs = MakeShared<FJsonObject>(); DigestArgs->SetStringField(TEXT("assetPath"), State->Blueprint->GetPathName()); DigestArgs->SetStringField(TEXT("graphName"), TEXT("")); DigestArgs->SetStringField(TEXT("focus"), TEXT("errors"));
    Client->CallTool(TEXT("KHUTutorTools.KHUTutorReadToolset.GetBlueprintGraphDigest"), KHUMcpProtocol::WriteObject(DigestArgs), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Digest"), MoveTemp(R)); });
    Client->CallTool(TEXT("KHUTutorTools.KHUTutorReadToolset.GetBlueprintCompileErrors"), KHUMcpProtocol::WriteObject(Args), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Errors"), MoveTemp(R)); });
    auto ShowArgs = MakeShared<FJsonObject>(); ShowArgs->SetStringField(TEXT("assetPath"), State->Blueprint->GetPathName()); ShowArgs->SetStringField(TEXT("nodeRef"), TEXT("#1"));
    Client->CallTool(TEXT("KHUTutorTools.KHUTutorShowToolset.FocusGraphNode"), KHUMcpProtocol::WriteObject(ShowArgs), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Show"), MoveTemp(R)); });
    DigestArgs->SetStringField(TEXT("graphName"), TEXT("MissingGraph"));
    Client->CallTool(TEXT("KHUTutorTools.KHUTutorReadToolset.GetBlueprintGraphDigest"), KHUMcpProtocol::WriteObject(DigestArgs), [State](FKHUMcpResult R) { State->Results.Add(TEXT("BadGraph"), MoveTemp(R)); });
    Client->CallTool(TEXT("KHUTutorTools.KHUTutorReadToolset.GetBlueprintGraphDigest"), TEXT("{\"assetPath\":\"../Other\",\"graphName\":\"\"}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("BadPath"), MoveTemp(R)); });
    Client->CallTool(TEXT("KHUTutorTools.KHUTutorReadToolset.GetBlueprintGraphDigest"), KHUMcpProtocol::WriteObject(Args), [State](FKHUMcpResult R) { State->Results.Add(TEXT("MissingArgument"), MoveTemp(R)); });
    AddCommand(new FD5GraphCheck(this, State)); return true;
}
#endif

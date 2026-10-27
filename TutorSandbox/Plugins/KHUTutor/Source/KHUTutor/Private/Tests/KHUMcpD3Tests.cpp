#include "KHUTutorModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FD3LiveState
    {
        TMap<FString, FKHUMcpResult> Results;
        double Deadline = FPlatformTime::Seconds() + 30;
    };
    class FD3ReadCheck final : public IAutomationLatentCommand
    {
    public:
        FD3ReadCheck(FAutomationTestBase* InTest, TSharedRef<FD3LiveState> InState) : Test(InTest), State(InState) {}
        virtual bool Update() override
        {
            if (State->Results.Num() < 4 && FPlatformTime::Seconds() < State->Deadline) { return false; }
            if (State->Results.Num() != 4) { Test->AddError(TEXT("D3 real read tools did not complete within 30 seconds")); return true; }
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("KHUTutor");
            IFileManager::Get().MakeDirectory(*Directory, true);
            for (const auto& Pair : State->Results)
            {
                const bool bExpectedSuccess = Pair.Key != TEXT("Error");
                Test->TestEqual(*Pair.Key, Pair.Value.bSuccess, bExpectedSuccess);
                Test->TestTrue(TEXT("Tool text limited to 6000 characters"), Pair.Value.Text.Len() <= 6000);
                if (bExpectedSuccess && !Pair.Value.bSuccess) { Test->AddError(Pair.Value.Error); }
                auto Receipt = MakeShared<FJsonObject>();
                Receipt->SetBoolField(TEXT("success"), Pair.Value.bSuccess);
                Receipt->SetStringField(TEXT("text"), Pair.Value.Text); Receipt->SetStringField(TEXT("error"), Pair.Value.Error);
                Receipt->SetNumberField(TEXT("http_status"), Pair.Value.HttpStatus);
                Test->TestTrue(TEXT("Receipt saved"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Receipt),
                    *(Directory / (TEXT("D3_") + Pair.Key + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            }
            Test->TestTrue(TEXT("Context string unwrapped"), State->Results[TEXT("Context")].Text.StartsWith(TEXT("ENGINE ")));
            Test->TestTrue(TEXT("Digest string unwrapped"), State->Results[TEXT("Log")].Text.StartsWith(TEXT("LOG ")));
            Test->TestTrue(TEXT("Invalid path explicit error"), State->Results[TEXT("Error")].Error.StartsWith(TEXT("ERROR:")));
            return true;
        }
    private:
        FAutomationTestBase* Test;
        TSharedRef<FD3LiveState> State;
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUD3LiveReadTest, "KHUTutor.Mcp.Live.D3ReadTools",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKHUD3LiveReadTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorMcpSmoke"))) { AddInfo(TEXT("D3 live test skipped without -McpSmoke")); return true; }
    auto State = MakeShared<FD3LiveState>();
    const auto Client = IKHUTutorModule::Get().GetMcpClient();
    const FString Prefix = TEXT("KHUTutorTools.KHUTutorReadToolset.");
    Client->CallTool(Prefix + TEXT("GetTutorContext"), TEXT("{}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Context"), MoveTemp(R)); });
    Client->CallTool(Prefix + TEXT("GetLogDigest"), TEXT("{\"minSeverity\":\"Warning\",\"sinceSeconds\":600,\"maxLines\":40}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Log"), MoveTemp(R)); });
    Client->CallTool(Prefix + TEXT("GetBlueprintCompileErrors"), TEXT("{\"assetPath\":\"\"}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Blueprint"), MoveTemp(R)); });
    Client->CallTool(Prefix + TEXT("GetBlueprintCompileErrors"), TEXT("{\"assetPath\":\"../Other\"}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Error"), MoveTemp(R)); });
    AddCommand(new FD3ReadCheck(this, State)); return true;
}
#endif

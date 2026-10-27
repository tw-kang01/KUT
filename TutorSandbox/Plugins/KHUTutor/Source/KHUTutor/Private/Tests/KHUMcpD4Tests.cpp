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
    struct FD4LiveState
    {
        TMap<FString, FKHUMcpResult> Results;
        double Deadline = FPlatformTime::Seconds() + 30;
    };
    class FD4ReadCheck final : public IAutomationLatentCommand
    {
    public:
        FD4ReadCheck(FAutomationTestBase* InTest, TSharedRef<FD4LiveState> InState) : Test(InTest), State(InState) {}
        virtual bool Update() override
        {
            if (State->Results.Num() < 4 && FPlatformTime::Seconds() < State->Deadline) { return false; }
            if (State->Results.Num() != 4) { Test->AddError(TEXT("D4 real tools did not complete within 30 seconds")); return true; }
            const FString Dir = FPaths::ProjectSavedDir() / TEXT("KHUTutor"); IFileManager::Get().MakeDirectory(*Dir, true);
            for (const auto& Pair : State->Results)
            {
                const bool Expected = Pair.Key == TEXT("Cpp") || Pair.Key == TEXT("Source");
                Test->TestEqual(*Pair.Key, Pair.Value.bSuccess, Expected);
                Test->TestTrue(TEXT("D4 text limited to 6000 characters"), Pair.Value.Text.Len() <= 6000);
                if (Expected && !Pair.Value.bSuccess) { Test->AddError(Pair.Value.Error); }
                auto Receipt = MakeShared<FJsonObject>(); Receipt->SetBoolField(TEXT("success"), Pair.Value.bSuccess);
                Receipt->SetStringField(TEXT("text"), Pair.Value.Text); Receipt->SetStringField(TEXT("error"), Pair.Value.Error);
                Receipt->SetNumberField(TEXT("http_status"), Pair.Value.HttpStatus);
                Test->TestTrue(TEXT("D4 receipt saved"), FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Receipt),
                    *(Dir / (TEXT("D4_") + Pair.Key + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
            }
            Test->TestTrue(TEXT("Cpp returned a sensor result"), State->Results[TEXT("Cpp")].Text.StartsWith(TEXT("CPP ")));
            Test->TestTrue(TEXT("Source excerpt numbered and unwrapped"), State->Results[TEXT("Source")].Text.StartsWith(TEXT("SOURCE "))
                && State->Results[TEXT("Source")].Text.Contains(TEXT("3| #include")));
            FString ExpectedCode;
            if (FParse::Value(FCommandLine::Get(), TEXT("KHUTutorExpectedCppError="), ExpectedCode))
            {
                Test->TestTrue(TEXT("Actual failed build observed through MCP"), State->Results[TEXT("Cpp")].Text.StartsWith(TEXT("CPP failed"))
                    && State->Results[TEXT("Cpp")].Text.Contains(ExpectedCode));
            }
            return true;
        }
    private:
        FAutomationTestBase* Test;
        TSharedRef<FD4LiveState> State;
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUD4LiveReadTest, "KHUTutor.Mcp.Live.D4ReadTools",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKHUD4LiveReadTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("KHUTutorMcpSmoke"))) { AddInfo(TEXT("D4 live test skipped without -McpSmoke")); return true; }
    auto State = MakeShared<FD4LiveState>(); const auto Client = IKHUTutorModule::Get().GetMcpClient();
    const FString Prefix = TEXT("KHUTutorTools.KHUTutorReadToolset.");
    Client->CallTool(Prefix + TEXT("GetCppBuildErrors"), TEXT("{\"maxErrors\":10}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Cpp"), MoveTemp(R)); });
    Client->CallTool(Prefix + TEXT("ReadProjectSource"), TEXT("{\"relativePath\":\"Source/TutorSandbox/TutorSandbox.cpp\",\"line\":3,\"radius\":2}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Source"), MoveTemp(R)); });
    Client->CallTool(Prefix + TEXT("ReadProjectSource"), TEXT("{\"relativePath\":\"Source/../Config/Test.cpp\",\"line\":1,\"radius\":1}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Traversal"), MoveTemp(R)); });
    Client->CallTool(Prefix + TEXT("ReadProjectSource"), TEXT("{\"relativePath\":\"C:/Windows/System.ini\",\"line\":1,\"radius\":1}"), [State](FKHUMcpResult R) { State->Results.Add(TEXT("Absolute"), MoveTemp(R)); });
    AddCommand(new FD4ReadCheck(this, State)); return true;
}
#endif

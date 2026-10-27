#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Toolsets/KHUTutorReadToolset.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUReadToolRegistrationTest,
    "KHUTutor.Tools.ReadToolRegistration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKHUReadToolRegistrationTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Read toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UKHUTutorReadToolset::StaticClass()));
    const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UKHUTutorReadToolset::StaticClass());
    TestTrue(TEXT("Schema contains fully qualified Ping"),
        Schema.Contains(TEXT("KHUTutorTools.KHUTutorReadToolset.Ping")));
    TestTrue(TEXT("Schema retains the actual lowercase message argument"), Schema.Contains(TEXT("\"message\"")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKHUReadToolIdentityTest,
    "KHUTutor.Tools.PingIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKHUReadToolIdentityTest::RunTest(const FString& Parameters)
{
    const FString Result = UKHUTutorReadToolset::Ping(TEXT("\ud55c\uae00 | test\n"));
    TestTrue(TEXT("Unicode echo survives and separators are sanitized"),
        Result.StartsWith(TEXT("PONG: \ud55c\uae00 / test ")));
    TestTrue(TEXT("Reply identifies the current project"),
        Result.Contains(FString::Printf(TEXT(" | project %s | project_file "), FApp::GetProjectName())));
    TestTrue(TEXT("Reply fits the tool result limit"), Result.Len() <= 6000);
    return true;
}
#endif

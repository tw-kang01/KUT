#include "KHUTutorModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace { constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductDependencies, "KHUTutor.Product.NoMcpDependency", Flags)
bool FProductDependencies::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("KHUTutor"));
    if (!Plugin) { AddError(TEXT("Product plugin is unavailable")); return false; }
    FString Text; TSharedPtr<FJsonObject> Descriptor;
    if (!FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("KHUTutor.uplugin"))) || !KHUMcpProtocol::ReadObject(Text, Descriptor))
    { AddError(TEXT("Product descriptor could not be read")); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Dependencies = nullptr;
    TestTrue(TEXT("Explicit owned tool registry dependency"), Descriptor->TryGetArrayField(TEXT("Plugins"), Dependencies));
    bool Registry = false;
    if (Dependencies)
    {
        for (const auto& V : *Dependencies)
        {
            const FString Name = V->AsObject()->GetStringField(TEXT("Name"));
            TestNotEqual(TEXT("MCP must not be a product dependency"), Name, FString(TEXT("ModelContextProtocol")));
            Registry |= Name == TEXT("ToolsetRegistry");
        }
    }
    TestTrue(TEXT("Local schema registry retained"), Registry);
    if (FParse::Param(FCommandLine::Get(), TEXT("KHUTutorNoMcp")))
    {
        const auto Mcp = IPluginManager::Get().FindPlugin(TEXT("ModelContextProtocol"));
        TestTrue(TEXT("MCP plugin is disabled throughout this verification"), !Mcp || !Mcp->IsEnabled());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductLocalTools, "KHUTutor.Product.PanelLocalTools", Flags)
bool FProductLocalTools::RunTest(const FString&)
{
    struct FState { int32 Completed = 0; FKHUMcpResult Ping, Rejected, Relations, Details; };
    const auto State = MakeShared<FState>(); const auto Tools = IKHUTutorModule::Get().CreatePanelTools();
    auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("message"), TEXT("product-local-only"));
    Tools->Execute(FKHUMcpClient::PingToolName(), Args, [State](FKHUMcpResult R) { State->Ping = MoveTemp(R); ++State->Completed; });
    Tools->Execute(TEXT("EditorToolset.Python.ExecutePython"), MakeShared<FJsonObject>(), [State](FKHUMcpResult R) { State->Rejected = MoveTemp(R); ++State->Completed; });
    auto RelationArgs = MakeShared<FJsonObject>(); RelationArgs->SetStringField(TEXT("worldScope"), TEXT("InvalidScope"));
    RelationArgs->SetStringField(TEXT("actorPath"), TEXT("")); RelationArgs->SetNumberField(TEXT("depth"), 1); RelationArgs->SetNumberField(TEXT("maxObjects"), 4);
    Tools->Execute(TEXT("KHUTutorTools.KHUTutorReadToolset.GetActorRelations"), RelationArgs, [State](FKHUMcpResult R) { State->Relations = MoveTemp(R); ++State->Completed; });
    auto DetailArgs = MakeShared<FJsonObject>(); DetailArgs->SetStringField(TEXT("objectPath"), TEXT("/Game/Missing")); DetailArgs->SetStringField(TEXT("worldScope"), TEXT("InvalidScope"));
    DetailArgs->SetStringField(TEXT("propertyNames"), TEXT("")); DetailArgs->SetNumberField(TEXT("maxProperties"), 12);
    Tools->Execute(TEXT("KHUTutorTools.KHUTutorReadToolset.GetObjectDetails"), DetailArgs, [State](FKHUMcpResult R) { State->Details = MoveTemp(R); ++State->Completed; });
    class FCheck final : public IAutomationLatentCommand
    {
    public:
        FCheck(FAutomationTestBase* InTest, TSharedRef<FState> InState, TSharedRef<IKHUToolExecutor> InTools)
            : Test(InTest), State(InState), Tools(InTools), Deadline(FPlatformTime::Seconds() + 10) {}
        virtual bool Update() override
        {
            if (State->Completed != 4 && FPlatformTime::Seconds() < Deadline) { return false; }
            Test->TestEqual(TEXT("All local callbacks completed"), State->Completed, 4);
            Test->TestTrue(TEXT("Panel factory reaches this editor without a server"), State->Ping.bSuccess && State->Ping.Text.Contains(TEXT("PONG: product-local-only")) && State->Ping.Text.Contains(TEXT("project TutorSandbox")));
            Test->TestEqual(TEXT("No HTTP response involved"), State->Ping.HttpStatus, 0);
            Test->TestFalse(TEXT("Foreign executable tool rejected"), State->Rejected.bSuccess);
            Test->TestTrue(TEXT("Owned-schema check remains enforced"), State->Rejected.Error.Contains(TEXT("invalid owned tool")));
            Test->AddInfo(TEXT("Relations result: ") + State->Relations.Error);
            Test->AddInfo(TEXT("Details result: ") + State->Details.Error);
            Test->TestTrue(TEXT("Relation tool reached its own scope guard"), !State->Relations.bSuccess && State->Relations.Error.Contains(TEXT("worldScope must be")));
            Test->TestTrue(TEXT("Details tool reached its own scope guard"), !State->Details.bSuccess && State->Details.Error.Contains(TEXT("worldScope must be")));
            Tools->Cancel(); return true;
        }
    private:
        FAutomationTestBase* Test; TSharedRef<FState> State; TSharedRef<IKHUToolExecutor> Tools; double Deadline;
    };
    AddCommand(new FCheck(this, State, Tools)); return true;
}
#endif

#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Toolsets/KHUTutorReadToolset.h"
#include "Toolsets/KHUTutorShowToolset.h"
#include "Digest/KHUGraphDigest.h"
#include "Digest/KHUNodeRefCache.h"
#include "HAL/IConsoleManager.h"
#include "Sensors/KHULogCapture.h"
#include "Sensors/KHUCompileWatcher.h"
#include "Sensors/KHUCppBuildWatcher.h"

DEFINE_LOG_CATEGORY_STATIC(LogKHUTutorDigest, Log, All);

class FKHUTutorToolsModule final : public IModuleInterface
{
public:
    /** Register the read-only toolset after editor subsystems are initialized. */
    virtual void StartupModule() override
    {
        FKHULogCapture::Get().Start();
        FKHUCppBuildWatcher::Get().Start();
        FKHUCompileWatcher::Get().Start();
        UToolsetRegistry::RegisterToolsetClass(UKHUTutorReadToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UKHUTutorShowToolset::StaticClass());
        MeasureCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("KHUTutor.Digest.Measure"), TEXT("Measure <loaded-/Game/Blueprint> [GraphName] with Focus=errors; local estimate only."),
            FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
            {
                if (Args.IsEmpty()) { UE_LOG(LogKHUTutorDigest, Display, TEXT("Usage: KHUTutor.Digest.Measure <AssetPath> [GraphName]")); return; }
                const FString Text = UKHUTutorReadToolset::GetBlueprintGraphDigest(Args[0], Args.Num() > 1 ? Args[1] : FString());
                const FTCHARToUTF8 Utf8(*Text);
                UE_LOG(LogKHUTutorDigest, Display, TEXT("KHU Graph Digest: chars=%d utf8_bytes=%d estimated_tokens=%d (chars/4 heuristic; not model tokenizer)\n%s"), Text.Len(), Utf8.Length(), (Text.Len() + 3) / 4, *Text);
            }), ECVF_Default);
    }

    /** Remove this module's toolset before unloading. */
    virtual void ShutdownModule() override
    {
        FKHUCompileWatcher::Get().Stop();
        FKHUCppBuildWatcher::Get().Stop();
        FKHULogCapture::Get().Stop();
        if (MeasureCommand) { IConsoleManager::Get().UnregisterConsoleObject(MeasureCommand, false); MeasureCommand = nullptr; }
        FKHUNodeRefCache::Get().Reset();
        if (UToolsetRegistry::IsAvailable())
        {
            UToolsetRegistry::UnregisterToolsetClass(UKHUTutorReadToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UKHUTutorShowToolset::StaticClass());
        }
    }
private:
    IConsoleCommand* MeasureCommand = nullptr;
};

IMPLEMENT_MODULE(FKHUTutorToolsModule, KHUTutorTools)

using UnrealBuildTool;

public class KHUTutorTools : ModuleRules
{
    public KHUTutorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Several .cpp files define same-named helpers in anonymous namespaces (Flags, Config, Safe, ...). A unity blob would merge
        // them into one translation unit and fail to compile, so every file is compiled on its own regardless of adaptive unity.
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "Engine", "UnrealEd", "Json", "Projects", "BlueprintGraph", "Kismet", "GraphEditor", "Slate", "SlateCore" });
        if (Target.Platform == UnrealTargetPlatform.Win64) { PrivateDependencyModuleNames.Add("LiveCoding"); }
    }
}

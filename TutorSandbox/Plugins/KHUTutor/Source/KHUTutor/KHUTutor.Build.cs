using UnrealBuildTool;

public class KHUTutor : ModuleRules
{
    public KHUTutor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Several .cpp files define same-named helpers in anonymous namespaces (Flags, Config, Safe, ...). A unity blob would merge
        // them into one translation unit and fail to compile, so every file is compiled on its own regardless of adaptive unity.
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "Json", "CoreUObject", "DeveloperSettings" });
        PrivateDependencyModuleNames.AddRange(new[] { "HTTP", "HTTPServer", "Sockets", "Projects", "Engine", "KHUTutorTools", "ToolsetRegistry", "Slate", "SlateCore", "ToolMenus", "Settings", "UnrealEd", "Kismet", "LevelEditor", "ApplicationCore" });
        if (Target.Platform == UnrealTargetPlatform.Win64) { PublicSystemLibraries.Add("Crypt32.lib"); }
    }
}

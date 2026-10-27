// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Minimal primary game module. TutorSandbox only hosts the KHUTutor plugin and its automation tests;
// the plugin itself has no dependency on this module.
public class TutorSandbox : ModuleRules
{
	public TutorSandbox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}

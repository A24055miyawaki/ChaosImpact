// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosImpact : ModuleRules
{
	public ChaosImpact(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"ApplicationCore",
			"EngineSettings",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"JoyShockLibrary4Unreal",
			"NetCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils"
		});

		// Keep the supplied logo available through the platform file layer in packaged builds.
		RuntimeDependencies.Add("$(ProjectDir)/Content/UI/TitleLogoTransparent.png", StagedFileType.UFS);

		PublicIncludePaths.AddRange(new string[] {
			"ChaosImpact",
			"ChaosImpact/Variant_Platforming",
			"ChaosImpact/Variant_Platforming/Animation",
			"ChaosImpact/Variant_Combat",
			"ChaosImpact/Variant_Combat/AI",
			"ChaosImpact/Variant_Combat/Animation",
			"ChaosImpact/Variant_Combat/Gameplay",
			"ChaosImpact/Variant_Combat/Interfaces",
			"ChaosImpact/Variant_Combat/UI",
			"ChaosImpact/Variant_SideScrolling",
			"ChaosImpact/Variant_SideScrolling/AI",
			"ChaosImpact/Variant_SideScrolling/Gameplay",
			"ChaosImpact/Variant_SideScrolling/Interfaces",
			"ChaosImpact/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

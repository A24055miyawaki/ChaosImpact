// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosImpact : ModuleRules
{
	public ChaosImpact(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// スマホ版: the Wi-Fi kept quick while the game is in front (ChaosImpact_UPL.xml).
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(ModuleDirectory, "ChaosImpact_UPL.xml"));
		}

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
			"RHI",
			"RenderCore",
			"NetCore",
			"ProceduralMeshComponent",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			// Internet rooms through the relay (ChaosImpactRelay).
			"Sockets",
			"WebSockets",
			"Json",
			"MoviePlayer",
			"MediaAssets",
			// ペイント: the pictures kept as PNG.
			"ImageCore",
			// The exploration map's terrain builder (OpenWorld/).
			"MeshDescription",
			"StaticMeshDescription",
			"AssetRegistry"
		});

		// The PlayStation / Switch pads (JoyShockLibrary4Unreal) are Windows only; elsewhere (スマホ版) the game builds without
		// them (ChaosImpactJoyShock.h).
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("JoyShockLibrary4Unreal");
			PublicDefinitions.Add("WITH_JOYSHOCK=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_JOYSHOCK=0");
		}

		// Keep the supplied logo available through the platform file layer in packaged builds.
		RuntimeDependencies.Add("$(ProjectDir)/Content/UI/TitleLogoTransparent.png", StagedFileType.UFS);
		// The stage select's pictures, read as loose files too.
		RuntimeDependencies.Add("$(ProjectDir)/Content/UI/StageSelect/*.png", StagedFileType.UFS);
		// Recorded tutorials are loose movies so Windows Media Foundation can open them.
		RuntimeDependencies.Add("$(ProjectDir)/Content/Movies/Controls/*.mp4", StagedFileType.NonUFS);

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

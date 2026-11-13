#if WITH_DEV_AUTOMATION_TESTS
// How heavy the smoke ball's cloud is to draw when it fills the view (a player standing in it): the GPU's and the whole
// frame's time, averaged, with nothing, with one cloud on the player, and with three clouds round them.
#include "Misc/AutomationTest.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactHazardZone.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UnrealClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ChaosImpactSettings.h"

namespace
{
	class FPerfSmokeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPerfSmokeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (!Pawn)
			{
				if (Now - Started > 60.0) { Test->AddError(TEXT("No player")); return true; }
				return false;
			}
			if (PC->IsPaused()) { PC->SetPause(false); }
			// Sampling: the GPU's time and the frame's, each frame.
			if (Sampling)
			{
				GpuSum += FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles(0));
				FrameSum += FApp::GetDeltaTime() * 1000.0;
				++Frames;
			}
			if (Now < NextAt)
			{
				return false;
			}
			const auto Report = [this](const TCHAR* What)
			{
				UE_LOG(LogTemp, Display, TEXT("PERFSMOKE %-12s gpu %.2f ms  frame %.2f ms  (%d frames)"), What,
					Frames ? GpuSum / Frames : 0.0, Frames ? FrameSum / Frames : 0.0, Frames);
				GpuSum = FrameSum = 0.0;
				Frames = 0;
			};
			const FVector Feet = Pawn->GetActorLocation() - FVector(0, 0, 90);
			switch (Step++)
			{
			case 0:
			{
				// -CIPerfQuality=<0..3>: that overall quality for this run (applied, not saved); -CIPerfScale=<percent>.
				int32 Quality = -1;
				int32 Scale = 0;
				UGameUserSettings* Settings = GEngine->GetGameUserSettings();
				if (Settings && FParse::Value(FCommandLine::Get(), TEXT("CIPerfQuality="), Quality))
				{
					Settings->SetOverallScalabilityLevel(Quality);
					Settings->ApplyNonResolutionSettings();
				}
				if (Settings && FParse::Value(FCommandLine::Get(), TEXT("CIPerfScale="), Scale))
				{
					// The game's own path (the 解像度スケール setting), put back at the end.
					SavedScale = ChaosImpactSettings::GetResolutionScale();
					ChaosImpactSettings::SetResolutionScale(Scale);
				}
				const IConsoleVariable* Percent = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
				const IConsoleVariable* Translucency = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SeparateTranslucencyScreenPercentage"));
				const IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"));
				UE_LOG(LogTemp, Display, TEXT("PERFSMOKE settings quality %d screen%% %.0f translucency%% %.0f shadows %d"),
					Settings ? Settings->GetOverallScalabilityLevel() : -1, Percent ? Percent->GetFloat() : -1.0f,
					Translucency ? Translucency->GetFloat() : -1.0f, Shadows ? Shadows->GetInt() : -1);
				NextAt = Now + 4.0;
				break;
			}
			case 1: Sampling = true; NextAt = Now + 3.0; break;
			case 2:
				Report(TEXT("none"));
				Sampling = false;
				AChaosImpactHazardZone::SpawnVisual(World, EChaosImpactBallType::Smoke, Feet, ChaosImpactBallTypes::SmokeRadius, 9.0f);
				NextAt = Now + 1.2;
				break;
			case 3: Sampling = true; NextAt = Now + 3.0; break;
			case 4:
				Report(TEXT("one cloud"));
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerfQA/SmokeOne.png")), true, false);
				Sampling = false;
				NextAt = Now + 5.5;
				break;
			case 5:
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const FVector Offset = FRotator(0.0f, Index * 120.0f, 0.0f).Vector() * 350.0f;
					AChaosImpactHazardZone::SpawnVisual(World, EChaosImpactBallType::Smoke, Feet + Offset, ChaosImpactBallTypes::SmokeRadius, 9.0f);
				}
				NextAt = Now + 1.2;
				break;
			case 6: Sampling = true; NextAt = Now + 3.0; break;
			case 7:
				Report(TEXT("three clouds"));
				if (SavedScale > 0) { ChaosImpactSettings::SetResolutionScale(SavedScale); }
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerfQA/SmokeThree.png")), true, false);
				return true;
			default:
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 SavedScale = 0;
		bool Sampling = false;
		double GpuSum = 0.0;
		double FrameSum = 0.0;
		int32 Frames = 0;
		double NextAt = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfSmokeTest, "ChaosImpact.Perf.Smoke", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPerfSmokeTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPerfSmokeCommand(this));
	return true;
}
#endif

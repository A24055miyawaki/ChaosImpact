#if WITH_DEV_AUTOMATION_TESTS
// A look at the White Ball House's floor close up (Saved/PresidentQA/Probe_*.png) and its textures' state in the log.
// Run on /Game/ChaosImpact/solo/Lvl_Solo_30_WhiteBallHouse?CISolo=1?CIStage=30
#include "Misc/AutomationTest.h"
#include "ChaosImpactWhiteHall.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	class FHallProbeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHallProbeCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			if (!PC) { return false; }
			const FVector Spots[][2] = {
				{FVector(3000.0f, -300.0f, 400.0f), FVector(3000.0f, 0.0f, 0.0f)},
				{FVector(2200.0f, -800.0f, 1600.0f), FVector(3400.0f, 0.0f, 0.0f)},
				{FVector(-1500.0f, 0.0f, 300.0f), FVector(0.0f, 0.0f, 0.0f)},
			};
			if (Step == 0)
			{
				WaitUntil = Now + 4.0;
				Step = 1;
				if (UTexture2D* Floor = LoadObject<UTexture2D>(nullptr, TEXT("/Game/ChaosImpact/solo/Stage30/Textures/T_Hall_FloorCheck.T_Hall_FloorCheck")))
				{
					UE_LOG(LogTemp, Display, TEXT("HALLPROBE floor tex %dx%d mips %d resident %d neverstream %d"), Floor->GetSizeX(), Floor->GetSizeY(),
						Floor->GetNumMips(), Floor->GetNumResidentMips(), Floor->NeverStream ? 1 : 0);
				}
				return false;
			}
			if (Step - 1 < UE_ARRAY_COUNT(Spots))
			{
				const FVector From = Spots[Step - 1][0];
				const FVector To = Spots[Step - 1][1];
				if (!Camera.IsValid())
				{
					Camera = World->SpawnActor<ACameraActor>(From, (To - From).Rotation());
					Camera->GetCameraComponent()->bConstrainAspectRatio = false;
					PC->SetViewTarget(Camera.Get());
				}
				Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
				WaitUntil = Now + 1.0;
				if (bShotNext)
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PresidentQA"),
						FString::Printf(TEXT("Probe_%d.png"), Step)), false, false);
					bShotNext = false;
					++Step;
				}
				else
				{
					bShotNext = true;
				}
				return false;
			}
			return true;
		}
	private:
		FAutomationTestBase* Test = nullptr;
		int32 Step = 0;
		bool bShotNext = false;
		double WaitUntil = 0.0;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHallProbeTest, "ChaosImpact.Tools.HallProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChaosImpactHallProbeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHallProbeCommand(this));
	return true;
}
#endif

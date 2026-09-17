#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Run in training with CPUs (?CITraining=1?CICPUCount=3?CITargets=0): films the character model close up while
	 * standing, running, holding a ball, charging and throwing, then the CPUs in their colours.
	 */
	class FToonCharacterCommand : public IAutomationLatentCommand
	{
	public:
		explicit FToonCharacterCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !PC->IsGameplayActive())
			{
				if (Now - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No playable character."));
					return true;
				}
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ToonQA"), Name), false, false);
			};
			const auto Film = [this, World, Player](const FVector& Offset)
			{
				if (!Camera.IsValid())
				{
					FActorSpawnParameters Parameters;
					Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Camera = World->SpawnActor<ACameraActor>(Parameters);
					Camera->GetCameraComponent()->SetFieldOfView(50.0f);
				}
				const FVector Focus = Player->GetActorLocation() + FVector(0.0f, 0.0f, 10.0f);
				const FVector Eye = Focus + Player->GetActorRotation().RotateVector(Offset);
				Camera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
				if (APlayerController* Controller = Cast<APlayerController>(Player->GetController()))
				{
					Controller->SetViewTarget(Camera.Get());
				}
			};
			// Front, a little to the side and above.
			const FVector Front(330.0f, 120.0f, 90.0f);
			if (Stage > 0 && Stage < 9)
			{
				Film(Stage == 3 ? FVector(40.0f, 360.0f, 70.0f) : Front);
			}
			if (Stage == 2)
			{
				Player->AddMovementInput(Player->GetActorRightVector(), 1.0f);
			}
			if (Now < NextAt)
			{
				return false;
			}
			switch (Stage)
			{
			case 0:
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
				}
				// A CPU's ball thrown before they stopped may still knock the player out or freeze them.
				if (Player->IsEliminated() || Player->IsIceFrozen())
				{
					NextAt = Now + 0.5;
					return false;
				}
				Stage = 1;
				NextAt = Now + 2.0;
				return false;
			case 1:
				Shot(TEXT("Toon-01-Idle.png"));
				Stage = 2;
				NextAt = Now + 0.9;
				return false;
			case 2:
				Shot(TEXT("Toon-02-Run.png"));
				Stage = 3;
				NextAt = Now + 0.02;
				return false;
			case 3:
				Stage = 4;
				NextAt = Now + 0.6;
				return false;
			case 4:
			{
				Shot(TEXT("Toon-03-RunSide.png"));
				// Hand the player a ball straight from a spawner.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->IsPickup() && Player->TryPickupBall(*It))
					{
						break;
					}
				}
				Test->TestTrue(TEXT("The player holds a ball"), Player->GetCarriedBallCount() > 0);
				Stage = 5;
				NextAt = Now + 1.0;
				return false;
			}
			case 5:
				Shot(TEXT("Toon-04-Hold.png"));
				Player->BeginThrowInput();
				Stage = 6;
				NextAt = Now + 0.6;
				return false;
			case 6:
				Shot(TEXT("Toon-05-Charge.png"));
				Player->EndThrowInput();
				Stage = 7;
				NextAt = Now + 0.1;
				return false;
			case 7:
				Shot(TEXT("Toon-06-Throw.png"));
				Stage = 8;
				NextAt = Now + 0.15;
				return false;
			case 8:
				Shot(TEXT("Toon-07-Release.png"));
				Stage = 9;
				NextAt = Now + 0.5;
				return false;
			case 9:
			{
				// Everyone at once, from above, to see the colours.
				FVector Centre = Player->GetActorLocation();
				int32 Count = 1;
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Player)
					{
						It->SetActorLocation(Player->GetActorLocation() + FVector(0.0f, 160.0f * Count, 0.0f), false, nullptr,
							ETeleportType::TeleportPhysics);
						Centre += It->GetActorLocation();
						++Count;
					}
				}
				Centre /= Count;
				if (Camera.IsValid())
				{
					const FVector Eye = Centre + FVector(520.0f, 0.0f, 260.0f);
					Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
				}
				Stage = 10;
				NextAt = Now + 1.0;
				return false;
			}
			case 10:
				Shot(TEXT("Toon-08-Colours.png"));
				Stage = 11;
				NextAt = Now + 0.6;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactToonCharacterTest, "ChaosImpact.Training.ToonCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactToonCharacterTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FToonCharacterCommand(this));
	return true;
}

#endif

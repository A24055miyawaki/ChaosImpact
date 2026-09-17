#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
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
	 * Run in training with CPUs (?CITraining=1?CICPUCount=3?CITargets=0): films the player's character from the side
	 * while it runs and then walks, a frame every few hundredths of a second (Saved/ToonQA/Gait-*.png), and checks that
	 * the CPUs are spread over the roster.
	 */
	class FToonGaitCommand : public IAutomationLatentCommand
	{
	public:
		explicit FToonGaitCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (Stage == 0)
			{
				if (!bStarted)
				{
					bStarted = true;
					SettledAt = Now + 2.0;
					TArray<int32> Characters;
					for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
					{
						It->SetActorTickEnabled(false);
						if (const AChaosImpactPlayerState* State = It->GetPlayerState<AChaosImpactPlayerState>())
						{
							Characters.AddUnique(State->CharacterIndex);
						}
					}
					Test->AddInfo(FString::Printf(TEXT("CPU characters in use: %d"), Characters.Num()));
					Test->TestTrue(TEXT("CPUs use more than one character"), Characters.Num() > 1);
				}
				if (Now < SettledAt)
				{
					return false;
				}
				Origin = Player->GetActorLocation();
				Stage = 1;
				StageStartedAt = Now;
			}

			// Back and forth along one line so the level's walls are never reached.
			const float Elapsed = static_cast<float>(Now - StageStartedAt);
			const bool bWalking = Stage == 2;
			if (Heading.IsZero() || FVector::DotProduct(Player->GetActorLocation() - Origin, Heading) > 900.0f)
			{
				Heading = Heading.IsZero() ? FVector(1.0f, 0.0f, 0.0f) : -Heading;
			}
			Player->AddMovementInput(Heading, bWalking ? 0.3f : 1.0f);

			if (!Camera.IsValid())
			{
				FActorSpawnParameters Parameters;
				Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Camera = World->SpawnActor<ACameraActor>(Parameters);
				Camera->GetCameraComponent()->SetFieldOfView(45.0f);
				PC->SetViewTarget(Camera.Get());
			}
			const FVector Focus = Player->GetActorLocation() + FVector(0.0f, 0.0f, -10.0f);
			const FVector Eye = Focus + FVector(0.0f, 380.0f, 40.0f);
			Camera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());

			constexpr int32 Frames = 10;
			constexpr float FirstShotAt = 0.35f;
			constexpr float ShotEvery = 0.045f;
			const int32 Due = FMath::FloorToInt((Elapsed - FirstShotAt) / ShotEvery);
			if (Due >= 0 && Due < Frames && Due >= NextFrame && Now >= NextShotAt)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ToonQA"),
					FString::Printf(TEXT("Gait-%s-%02d.png"), bWalking ? TEXT("Walk") : TEXT("Run"), NextFrame)), false, false);
				++NextFrame;
				NextShotAt = Now + 0.02;
			}
			if (NextFrame >= Frames && Now >= NextShotAt + 0.1)
			{
				if (Stage == 2)
				{
					return true;
				}
				Stage = 2;
				StageStartedAt = Now;
				NextFrame = 0;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double SettledAt = 0.0;
		double StageStartedAt = 0.0;
		double NextShotAt = 0.0;
		int32 Stage = 0;
		int32 NextFrame = 0;
		bool bStarted = false;
		FVector Origin = FVector::ZeroVector;
		FVector Heading = FVector::ZeroVector;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactToonGaitTest, "ChaosImpact.Training.ToonGait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactToonGaitTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FToonGaitCommand(this));
	return true;
}

#endif

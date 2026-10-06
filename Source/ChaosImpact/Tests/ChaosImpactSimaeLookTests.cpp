#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBird.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The shima-enaga ball's show, for the eye: one appears on the ground beside the player (arrival flash), waits
	 * there (birds circling, ripples, sparkles, pillar), then another is thrown past (escort and shed down) and bursts
	 * (the flock's release). Screenshots go to Saved/SimaeLookQA. Training arena with one CPU, stilled.
	 */
	class FSimaeLookCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSimaeLookCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				return true;
			}
			if (!World || !Player || Now < NextAt)
			{
				return false;
			}
			const FVector Toward = Player->GetActorForwardVector().GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
			const auto SpawnSimae = [World, Player](const FVector& At) -> AChaosImpactBall*
			{
				const FTransform Where(FRotator::ZeroRotator, At);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
					Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(EChaosImpactBallType::Simae);
				Ball->FinishSpawning(Where);
				return Ball;
			};
			switch (Stage)
			{
			case 0:
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					if (APawn* CPU = It->GetPawn())
					{
						CPU->SetActorLocation(Player->GetActorLocation() + Toward * 700.0f, false, nullptr, ETeleportType::TeleportPhysics);
					}
				}
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					World->GetTimerManager().ClearAllTimersForObject(*It);
				}
				// One appears on the ground beside the player, as on a spawn pad.
				Waiting = SpawnSimae(Player->GetActorLocation() + Toward * 260.0f + Side * 120.0f - FVector::UpVector * 50.0f);
				Waiting->MakePickup();
				Stage = 1;
				NextAt = Now + 0.18;
				return false;
			case 1:
				Shoot(TEXT("1_Arrival"));
				Stage = 2;
				NextAt = Now + 1.6;
				return false;
			case 2:
				Shoot(TEXT("2_Waiting"));
				Stage = 3;
				NextAt = Now + 0.8;
				return false;
			case 3:
			{
				// Another, thrown across in front of the player toward the stilled CPU.
				AChaosImpactBall* Thrown = SpawnSimae(Player->GetActorLocation() + Side * -260.0f + FVector::UpVector * 30.0f);
				Thrown->Launch((Toward * 700.0f + Side * 260.0f).GetSafeNormal2D(), 1100.0f, EChaosImpactBallFlightMode::Arc, 0.0f);
				Flying = Thrown;
				Stage = 4;
				NextAt = Now + 0.3;
				return false;
			}
			case 4:
				Shoot(TEXT("3_Flight"));
				Stage = 5;
				return false;
			case 5:
				if (Flying.IsValid() && !Flying->HasDetonated())
				{
					return false;
				}
				Stage = 6;
				NextAt = Now + 0.12;
				return false;
			case 6:
				Shoot(TEXT("4_Release"));
				Stage = 7;
				NextAt = Now + 0.5;
				return false;
			case 7:
				Shoot(TEXT("5_Flock"));
				Test->TestTrue(TEXT("The waiting ball is still there"), Waiting.IsValid());
				Stage = 8;
				NextAt = Now + 1.0;
				return false;
			default:
				return true;
			}
		}

	private:
		static void Shoot(const TCHAR* Name)
		{
			FScreenshotRequest::RequestScreenshot(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimaeLookQA"), FString(Name) + TEXT(".png")), true, false);
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TWeakObjectPtr<AChaosImpactBall> Waiting;
		TWeakObjectPtr<AChaosImpactBall> Flying;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSimaeLookTest, "ChaosImpact.Training.SimaeLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSimaeLookTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSimaeLookCommand(this));
	return true;
}

#endif

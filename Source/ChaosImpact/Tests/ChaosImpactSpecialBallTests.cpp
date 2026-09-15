#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindSpecialBallTestController()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	void CaptureSpecialBall(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpecialBallQA"), Name), true, false);
	}

	AChaosImpactBall* SpawnTypedBall(UWorld* World, const EChaosImpactBallType Type, const FVector& Location,
		APawn* Thrower)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
			SpawnTransform, Thrower, Thrower, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Ball)
		{
			Ball->SetBallType(Type);
			Ball->FinishSpawning(SpawnTransform);
		}
		return Ball;
	}

	int32 CountZones(UWorld* World, const EChaosImpactBallType Type)
	{
		int32 Count = 0;
		for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
		{
			Count += It->GetZoneType() == Type ? 1 : 0;
		}
		return Count;
	}

	class FSpecialBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSpecialBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSpecialBallTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !Player)
			{
				if (Now - StartedAt < 40.0)
				{
					return false;
				}
				Test->AddError(TEXT("No training player was created."));
				return true;
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (Stage > 0 && !CPU)
			{
				Test->AddError(TEXT("The CPU character disappeared."));
				return true;
			}
			const FVector Toward = FVector(1.0f, 0.35f, 0.0f).GetSafeNormal();
			const auto Launch = [&](const EChaosImpactBallType Type)
			{
				const FVector Direction = (CPU->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
				if (AChaosImpactBall* Ball = SpawnTypedBall(World, Type,
					Player->GetActorLocation() + Direction * 90.0f + FVector(0.0f, 0.0f, 30.0f), Player))
				{
					Ball->Launch(Direction, 2100.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
			};

			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
					// A standing target: the CPU would otherwise dodge the test throws and throw back.
					It->SetActorTickEnabled(false);
				}
				CPU = CPUCharacter.Get();
				if (!CPU || !PC->IsGameplayActive())
				{
					if (Now - StartedAt < 40.0)
					{
						return false;
					}
					Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
					return true;
				}
				Origin = Player->GetActorLocation();
				// The player may already have collected spawner balls while the level started.
				Player->ResetForOnlineMatch(Origin, Player->GetActorRotation());
				CPU->SetActorLocation(Origin + Toward * 430.0f, false, nullptr, ETeleportType::TeleportPhysics);

				AChaosImpactBall* FireBall = SpawnTypedBall(World, EChaosImpactBallType::Fire, Origin + FVector(0, 0, 400), nullptr);
				AChaosImpactBall* IceBall = SpawnTypedBall(World, EChaosImpactBallType::Ice, Origin + FVector(0, 0, 400), nullptr);
				FireBall->MakePickup();
				IceBall->MakePickup();
				Test->TestTrue(TEXT("A fire ball can be picked up"), Player->TryPickupBall(FireBall));
				Test->TestTrue(TEXT("An ice ball can be picked up"), Player->TryPickupBall(IceBall));
				FireBall->Destroy();
				IceBall->Destroy();
				Test->TestEqual(TEXT("Right hand holds the first ball picked up"),
					Player->GetCarriedBallType(0), EChaosImpactBallType::Fire);
				Test->TestEqual(TEXT("Left hand holds the second ball picked up"),
					Player->GetCarriedBallType(1), EChaosImpactBallType::Ice);

				// Pickups on display next to the player.
				SpawnTypedBall(World, EChaosImpactBallType::Fire, Origin + FVector(-60.0f, -230.0f, -58.0f), nullptr)->MakePickup();
				SpawnTypedBall(World, EChaosImpactBallType::Ice, Origin + FVector(-60.0f, 230.0f, -58.0f), nullptr)->MakePickup();
				Stage = 1;
				NextAt = Now + 1.0;
				return false;
			}
			case 1:
				CaptureSpecialBall(TEXT("SB-01-Inventory-Pickups.png"));
				CPU->SetActorLocation(Origin + Toward * 430.0f, false, nullptr, ETeleportType::TeleportPhysics);
				CPUHealthBefore = CPU->GetHealth();
				PlayerHealthBefore = Player->GetHealth();
				Launch(EChaosImpactBallType::Ice);
				Stage = 2;
				NextAt = Now + 0.08;
				return false;
			case 2:
				CaptureSpecialBall(TEXT("SB-02-Ice-Flight.png"));
				Stage = 3;
				NextAt = Now + 0.45;
				return false;
			case 3:
				Test->TestTrue(TEXT("An ice ball hitting a player leaves an ice zone"),
					CountZones(World, EChaosImpactBallType::Ice) >= 1);
				Test->TestTrue(TEXT("The directly hit player is encased in ice"), CPU->IsIceFrozen());
				Test->TestTrue(TEXT("The direct ice hit also deals the ball's damage"),
					CPU->GetHealth() < CPUHealthBefore);
				Test->TestTrue(TEXT("The frozen ground is slippery"),
					AChaosImpactHazardZone::IsSlipperyAt(World, CPU->GetActorLocation() - FVector(0.0f, 0.0f, 96.0f)));
				Test->TestFalse(TEXT("The thrower is not frozen by their own ice ball"), Player->IsIceFrozen());
				FrozenAt = CPU->GetActorLocation();
				CaptureSpecialBall(TEXT("SB-03-Ice-Impact-Frozen.png"));
				Stage = 4;
				NextAt = Now + 0.9;
				return false;
			case 4:
				Test->TestTrue(TEXT("A frozen CPU cannot move"),
					FVector::Dist2D(CPU->GetActorLocation(), FrozenAt) < 15.0f);
				CaptureSpecialBall(TEXT("SB-04-Ice-Floor-Frozen.png"));
				Stage = 5;
				NextAt = Now + 1.5;
				return false;
			case 5:
				Test->TestFalse(TEXT("The freeze wears off after a couple of seconds"), CPU->IsIceFrozen());
				// Away from the ice, so the fire is seen on its own.
				CPU->SetActorLocation(Origin + FVector(1.0f, -0.6f, 0.0f).GetSafeNormal() * 430.0f, false, nullptr,
					ETeleportType::TeleportPhysics);
				CPUHealthBefore = CPU->GetHealth();
				Launch(EChaosImpactBallType::Fire);
				Stage = 6;
				NextAt = Now + 0.08;
				return false;
			case 6:
				CaptureSpecialBall(TEXT("SB-05-Fire-Flight.png"));
				Stage = 7;
				NextAt = Now + 0.16;
				return false;
			case 7:
				CaptureSpecialBall(TEXT("SB-06-Fire-Explosion.png"));
				Stage = 8;
				NextAt = Now + 0.9;
				return false;
			case 8:
			{
				Test->TestTrue(TEXT("A fire ball hitting a player leaves a burning zone"),
					CountZones(World, EChaosImpactBallType::Fire) >= 1);
				Test->TestTrue(TEXT("The direct fire hit deals damage"),
					CPU->IsEliminated() || CPU->GetHealth() < CPUHealthBefore);
				CaptureSpecialBall(TEXT("SB-07-Fire-Burning.png"));
				// Hold the CPU inside the fire to check the burn damage.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					if (It->GetZoneType() == EChaosImpactBallType::Fire && !CPU->IsEliminated())
					{
						CPU->SetActorLocation(It->GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), false, nullptr,
							ETeleportType::TeleportPhysics);
					}
				}
				CPU->ApplyIceFreeze(1.6f);
				CPUHealthBefore = CPU->GetHealth();
				Stage = 9;
				NextAt = Now + 1.15;
				return false;
			}
			case 9:
				CaptureSpecialBall(TEXT("SB-07b-Fire-Burning-Later.png"));
				Test->TestTrue(TEXT("Standing in the burning zone keeps dealing damage"),
					CPU->IsEliminated() || CPU->GetHealth() < CPUHealthBefore);
				Test->TestEqual(TEXT("The thrower is never hurt by their own balls"), Player->GetHealth(), PlayerHealthBefore);
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Ice,
					Player->GetActorLocation() - FVector(0.0f, 0.0f, 60.0f), Player, nullptr);
				Stage = 10;
				NextAt = Now + 0.35;
				return false;
			case 10:
				Test->TestFalse(TEXT("Standing on one's own ice zone does not freeze"), Player->IsIceFrozen());
				Test->TestTrue(TEXT("Frozen ground lowers the player's grip"),
					Player->GetCharacterMovement()->GroundFriction < 1.0f);
				CaptureSpecialBall(TEXT("SB-08-Player-On-Ice.png"));
				Player->BeginThrowInput();
				Stage = 11;
				NextAt = Now + 0.25;
				return false;
			case 11:
				Player->EndThrowInput();
				Stage = 12;
				NextAt = Now + 0.6;
				return false;
			case 12:
				Test->TestEqual(TEXT("Throwing uses the right-hand ball first"), Player->GetCarriedBallCount(), 1);
				Test->TestEqual(TEXT("The left-hand ball moves to the next throw"),
					Player->GetCarriedBallType(0), EChaosImpactBallType::Ice);
				CaptureSpecialBall(TEXT("SB-09-HUD-After-Throw.png"));
				PC->ToggleTrainingOverlay();
				PC->CycleTrainingSummonBallType();
				Stage = 13;
				NextAt = Now + 0.7;
				return false;
			case 13:
			{
				Test->TestEqual(TEXT("The training panel cycles the summoned ball type"),
					PC->GetTrainingSummonBallType(), EChaosImpactBallType::Ice);
				int32 IceBefore = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					IceBefore += It->GetBallType() == EChaosImpactBallType::Ice && It->IsPickup() ? 1 : 0;
				}
				PC->SummonTrainingBall();
				int32 IceAfter = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					IceAfter += It->GetBallType() == EChaosImpactBallType::Ice && It->IsPickup() ? 1 : 0;
				}
				Test->TestEqual(TEXT("ボールを呼び出す places a pickup of the selected type"), IceAfter, IceBefore + 1);
				Stage = 14;
				NextAt = Now + 0.5;
				return false;
			}
			case 14:
				CaptureSpecialBall(TEXT("SB-10-Training-Panel.png"));
				Stage = 15;
				NextAt = Now + 0.4;
				return false;
			default:
				PC->CloseTrainingOverlay();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		FVector FrozenAt = FVector::ZeroVector;
		float CPUHealthBefore = 0.0f;
		float PlayerHealthBefore = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSpecialBallTest, "ChaosImpact.Training.SpecialBalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactSpecialBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSpecialBallCommand(this));
	return true;
}

#endif

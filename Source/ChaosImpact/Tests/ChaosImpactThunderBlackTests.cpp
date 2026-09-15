#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindThunderBlackController()
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

	void CaptureThunderBlack(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ThunderBlackQA"), Name), true, false);
	}

	AChaosImpactBall* SpawnThunderBlackBall(UWorld* World, const EChaosImpactBallType Type, const FVector& Location,
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

	int32 CountThunderBlackZones(UWorld* World, const EChaosImpactBallType Type)
	{
		int32 Count = 0;
		for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
		{
			Count += It->GetZoneType() == Type ? 1 : 0;
		}
		return Count;
	}

	class FThunderBlackCommand : public IAutomationLatentCommand
	{
	public:
		explicit FThunderBlackCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindThunderBlackController();
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
			const FVector AwayFromCPU = FVector(-1.0f, -0.25f, 0.0f).GetSafeNormal();
			const auto PlaceAt = [](AChaosImpactCharacter* Character, const FVector& Location)
			{
				Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			};
			const auto Throw = [&](const EChaosImpactBallType Type, const FVector& Direction,
				const EChaosImpactBallFlightMode Mode) -> AChaosImpactBall*
			{
				AChaosImpactBall* Ball = SpawnThunderBlackBall(World, Type,
					Player->GetActorLocation() + Direction * 90.0f + FVector(0.0f, 0.0f, 30.0f), Player);
				if (Ball)
				{
					// A slow, arcing throw: the thunder ball must ignore both.
					Ball->Launch(Direction, 1200.0f, Mode, 500.0f);
				}
				return Ball;
			};
			const auto Count = [World](const EChaosImpactBallType Type) { return CountThunderBlackZones(World, Type); };

			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				CPU = CPUCharacter.Get();
				// Before the CPU was stopped it may have knocked the player out; wait for the respawn, whose
				// teleport back to the start would otherwise land in the middle of the checks, and for the player
				// to land: level throws from a player still in the air fly over everyone's heads.
				if (!CPU || !PC->IsGameplayActive() || Player->IsEliminated()
					|| !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					if (Now - StartedAt < 40.0)
					{
						return false;
					}
					Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
					return true;
				}
				// Balls the CPU threw before it was stopped could still hit the player during the checks.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (!It->IsPickup() && !It->GetAttachParentActor())
					{
						It->Destroy();
					}
				}
				Origin = Player->GetActorLocation();
				Player->ResetForOnlineMatch(Origin, Player->GetActorRotation());
				PlayerHealth = Player->GetHealth();
				// Types 3 and 4 need the three-bit inventory slots.
				AChaosImpactBall* ThunderPickup = SpawnThunderBlackBall(World, EChaosImpactBallType::Thunder, Origin + FVector(0, 0, 400), nullptr);
				AChaosImpactBall* BlackPickup = SpawnThunderBlackBall(World, EChaosImpactBallType::Black, Origin + FVector(0, 0, 400), nullptr);
				ThunderPickup->MakePickup();
				BlackPickup->MakePickup();
				Test->TestTrue(TEXT("A thunder ball can be picked up"), Player->TryPickupBall(ThunderPickup));
				Test->TestTrue(TEXT("A black ball can be picked up"), Player->TryPickupBall(BlackPickup));
				ThunderPickup->Destroy();
				BlackPickup->Destroy();
				Test->TestEqual(TEXT("Right hand holds the thunder ball"), Player->GetCarriedBallType(0), EChaosImpactBallType::Thunder);
				Test->TestEqual(TEXT("Left hand holds the black ball"), Player->GetCarriedBallType(1), EChaosImpactBallType::Black);
				SpawnThunderBlackBall(World, EChaosImpactBallType::Thunder, Origin + FVector(-60.0f, -230.0f, -58.0f), nullptr)->MakePickup();
				SpawnThunderBlackBall(World, EChaosImpactBallType::Black, Origin + FVector(-60.0f, 230.0f, -58.0f), nullptr)->MakePickup();
				Stage = 1;
				NextAt = Now + 1.0;
				return false;
			}
			case 1:
				if (Player->IsEliminated() || CPU->IsEliminated())
				{
					// Knocked out by a ball still in flight when the CPU stopped: set up again after the respawn.
					Stage = 0;
					return false;
				}
				CaptureThunderBlack(TEXT("TB-01-Inventory-Pickups.png"));
				PlayerHealth = Player->GetHealth();
				// A black hole with an opponent at the edge of its reach and the thrower close by.
				HoleCentre = Origin + FVector(0.0f, -700.0f, 0.0f);
				PlaceAt(CPU, HoleCentre + FVector(560.0f, 0.0f, 0.0f));
				PlayerSpot = HoleCentre + FVector(-380.0f, 140.0f, 0.0f);
				PlaceAt(Player, PlayerSpot);
				CPUHealth = CPU->GetHealth();
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Black, HoleCentre - FVector(0.0f, 0.0f, 30.0f), Player, nullptr);
				Test->TestTrue(TEXT("A black hole opens"), Count(EChaosImpactBallType::Black) >= 1);
				PhaseStartedAt = Now;
				Stage = 2;
				NextAt = Now + 0.7;
				return false;
			case 2:
				// The game clock can trail the wall clock under load: wait for the pull to show, up to a few seconds.
				if (FVector::Dist2D(CPU->GetActorLocation(), HoleCentre) >= 480.0f && Now - PhaseStartedAt < 4.0)
				{
					return false;
				}
				CaptureThunderBlack(TEXT("TB-02-BlackHole-Pulling.png"));
				UE_LOG(LogTemp, Display, TEXT("TBTEST cpu %.0f from centre after %.2f s"),
					FVector::Dist2D(CPU->GetActorLocation(), HoleCentre), Now - PhaseStartedAt);
				Test->TestTrue(TEXT("The opponent is being drawn in"), FVector::Dist2D(CPU->GetActorLocation(), HoleCentre) < 480.0f);
				PhaseStartedAt = Now - 0.7;
				Stage = 3;
				NextAt = PhaseStartedAt + 2.2;
				return false;
			case 3:
				UE_LOG(LogTemp, Display, TEXT("TBTEST cpu %.0f from centre, player moved %.1f"),
					FVector::Dist2D(CPU->GetActorLocation(), HoleCentre), FVector::Dist2D(Player->GetActorLocation(), PlayerSpot));
				Test->TestTrue(TEXT("The opponent ends up at the centre"), FVector::Dist2D(CPU->GetActorLocation(), HoleCentre) < 130.0f);
				Test->TestTrue(TEXT("The thrower is not pulled"), FVector::Dist2D(Player->GetActorLocation(), PlayerSpot) < 15.0f);
				Test->TestEqual(TEXT("Being pulled does no damage"), CPU->GetHealth(), CPUHealth);
				CaptureThunderBlack(TEXT("TB-03-BlackHole-Centre.png"));
				Stage = 4;
				NextAt = PhaseStartedAt + AChaosImpactHazardZone::BlackHoleSeconds + 0.25;
				return false;
			case 4:
				CaptureThunderBlack(TEXT("TB-04-BlackHole-Collapse.png"));
				PlaceAt(CPU, HoleCentre + FVector(450.0f, 0.0f, 0.0f));
				Stage = 40;
				return false;
			case 40:
				// Wait for the hole's own clock to run out rather than the wall clock.
				if (!AChaosImpactHazardZone::GetBlackHolePullOffset(World, CPU, 0.1f).IsNearlyZero())
				{
					if (Now - PhaseStartedAt < 10.0)
					{
						PlaceAt(CPU, HoleCentre + FVector(450.0f, 0.0f, 0.0f));
						return false;
					}
					Test->AddError(TEXT("The black hole never closed."));
					return true;
				}
				ReleasedAt = CPU->GetActorLocation();
				Stage = 5;
				NextAt = Now + 0.8;
				return false;
			case 5:
				Test->TestTrue(TEXT("Once closed it pulls nobody"), FVector::Dist2D(CPU->GetActorLocation(), ReleasedAt) < 15.0f);
				// A thunder ball that meets nobody; the CPU's collision is off so a rebound cannot reach it.
				PlaceAt(Player, Origin);
				CPU->SetActorEnableCollision(false);
				ZonesBefore = Count(EChaosImpactBallType::Thunder);
				ThunderBall = Throw(EChaosImpactBallType::Thunder, AwayFromCPU, EChaosImpactBallFlightMode::Arc);
				PhaseStartedAt = Now;
				Stage = 6;
				NextAt = Now + 0.15;
				return false;
			case 6:
				CaptureThunderBlack(TEXT("TB-05-Thunder-Flight.png"));
				if (const AChaosImpactBall* Ball = ThunderBall.Get())
				{
					const FVector Velocity = Ball->GetBallVelocity();
					UE_LOG(LogTemp, Display, TEXT("TBTEST thunder velocity %s"), *Velocity.ToCompactString());
					Test->TestTrue(TEXT("A thunder ball flies at full speed however it was thrown"),
						FMath::IsNearlyEqual(static_cast<float>(Velocity.Size()), ChaosImpactBallTypes::ThunderSpeed, 5.0f));
					Test->TestTrue(TEXT("A thunder ball flies level"), FMath::Abs(Velocity.Z) < 1.0);
				}
				else
				{
					Test->AddError(TEXT("The thunder ball vanished right after the throw."));
				}
				Stage = 7;
				NextAt = PhaseStartedAt + 2.5;
				return false;
			case 7:
				Test->TestEqual(TEXT("Before three seconds it is still flying"), Count(EChaosImpactBallType::Thunder), ZonesBefore);
				Stage = 8;
				NextAt = PhaseStartedAt + ChaosImpactBallTypes::ThunderFlightSeconds + 0.05;
				return false;
			case 8:
				CaptureThunderBlack(TEXT("TB-06-Thunder-Burst.png"));
				Stage = 9;
				NextAt = Now + 0.25;
				return false;
			case 9:
				Test->TestTrue(TEXT("After three seconds the thunder ball bursts"), Count(EChaosImpactBallType::Thunder) > ZonesBefore);
				CPU->SetActorEnableCollision(true);
				PlaceAt(CPU, Origin + Toward * 700.0f);
				CPUHealth = CPU->GetHealth();
				ZonesBefore = Count(EChaosImpactBallType::Thunder);
				Throw(EChaosImpactBallType::Thunder, Toward, EChaosImpactBallFlightMode::Straight);
				Stage = 10;
				NextAt = Now + 0.17;
				return false;
			case 10:
				CaptureThunderBlack(TEXT("TB-07-Thunder-Hit.png"));
				Stage = 11;
				NextAt = Now + 0.4;
				return false;
			case 11:
			{
				Test->TestTrue(TEXT("Hitting an opponent bursts it at once"), Count(EChaosImpactBallType::Thunder) > ZonesBefore);
				Test->TestTrue(TEXT("The hit deals damage"), CPU->IsEliminated() || CPU->GetHealth() < CPUHealth);
				if (!CPU->IsEliminated())
				{
					// The lightning also strikes whoever stands nearby.
					CPUHealth = CPU->GetHealth();
					AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Thunder,
						CPU->GetActorLocation() + FVector(200.0f, 0.0f, -40.0f), Player, nullptr);
					Test->TestTrue(TEXT("An opponent near the lightning is struck"), CPU->IsEliminated() || CPU->GetHealth() < CPUHealth);
				}
				Test->TestEqual(TEXT("The thrower is never hurt by their own balls"), Player->GetHealth(), PlayerHealth);
				ZonesBefore = Count(EChaosImpactBallType::Black);
				Throw(EChaosImpactBallType::Black, AwayFromCPU, EChaosImpactBallFlightMode::Arc);
				Stage = 12;
				NextAt = Now + 0.2;
				return false;
			}
			case 12:
				CaptureThunderBlack(TEXT("TB-08-BlackBall-Flight.png"));
				Stage = 13;
				NextAt = Now + 1.8;
				return false;
			default:
				Test->TestTrue(TEXT("A thrown black ball opens a black hole where it lands"), Count(EChaosImpactBallType::Black) > ZonesBefore);
				CaptureThunderBlack(TEXT("TB-09-BlackBall-Landed.png"));
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double PhaseStartedAt = 0.0;
		int32 Stage = 0;
		int32 ZonesBefore = 0;
		FVector Origin = FVector::ZeroVector;
		FVector HoleCentre = FVector::ZeroVector;
		FVector PlayerSpot = FVector::ZeroVector;
		FVector ReleasedAt = FVector::ZeroVector;
		float CPUHealth = 0.0f;
		float PlayerHealth = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
		TWeakObjectPtr<AChaosImpactBall> ThunderBall;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactThunderBlackTest, "ChaosImpact.Training.ThunderBlackBalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactThunderBlackTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FThunderBlackCommand(this));
	return true;
}

#endif

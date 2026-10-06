#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTornado.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindBallBuffController()
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

	void CaptureBallBuff(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BallBuffQA"), Name), true, false);
	}

	AChaosImpactBall* SpawnBuffBall(UWorld* World, const EChaosImpactBallType Type, const FVector& Location, APawn* Thrower)
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

	/**
	 * The stronger balls, in the training arena with one CPU (?CITraining=1?CICPUCount=1?CITargets=0):
	 * a fire ball's flight leaves burning fires behind it, a normal ball bends onto someone just off its line,
	 * ice freezes further out, and a tornado makes the balls it throws around its owner's. Screenshots go to
	 * Saved/BallBuffQA. (The black hole's burning centre and the thunder ball's speed-up are in ThunderBlackBalls.)
	 */
	class FBallBuffCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBallBuffCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindBallBuffController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !Player)
			{
				return Waited(Now, TEXT("No training player was created."));
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (Stage > 0 && !CPU)
			{
				Test->AddError(TEXT("The CPU character disappeared."));
				return true;
			}
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
			const auto PlaceAt = [](AChaosImpactCharacter* Character, const FVector& Location)
			{
				Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			};
			// Full health and no balls, then moved: the reset's own teleport can refuse a spot it finds crowded.
			const auto Restore = [&PlaceAt](AChaosImpactCharacter* Character, const FVector& Location, const FRotator& Rotation)
			{
				Character->ResetForOnlineMatch(Character->GetActorLocation(), Rotation);
				PlaceAt(Character, Location);
			};
			const auto Throw = [&](const EChaosImpactBallType Type, const FVector& Direction) -> AChaosImpactBall*
			{
				AChaosImpactBall* Ball = SpawnBuffBall(World, Type, Player->GetActorLocation() + Direction * 90.0f + FVector(0, 0, 30), Player);
				if (Ball)
				{
					Ball->Launch(Direction, 2100.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
				return Ball;
			};
			// The player's trail only: the CPU may have thrown a fire ball of its own before the test began.
			const auto TrailFires = [World, Player]()
			{
				TArray<AChaosImpactHazardZone*> Fires;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					if (It->IsFireTrail() && It->GetSourcePawn() == Player)
					{
						Fires.Add(*It);
					}
				}
				return Fires;
			};

			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
					// A standing target that never dodges or throws back.
					It->SetActorTickEnabled(false);
				}
				CPU = CPUCharacter.Get();
				// A tornado the CPU released before it was stopped may still be carrying someone: let it run its course.
				bool bWindAbout = Player->IsCarriedByWind() || (CPU && CPU->IsCarriedByWind());
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					bWindAbout = true;
				}
				if (!CPU || !PC->IsGameplayActive() || Player->IsEliminated() || !Player->GetCharacterMovement()->IsMovingOnGround()
					|| bWindAbout)
				{
					return Waited(Now, TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (!It->IsPickup() && !It->GetAttachParentActor())
					{
						It->Destroy();
					}
				}
				Origin = Player->GetActorLocation();
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Restore(CPU, Origin - Toward * 700.0f, Toward.Rotation());
				// Whatever the CPU set off before it was stopped (a black hole, burning ground) goes, and its effects
				// are given a moment to clear from the screenshots.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				Stage = 100;
				NextAt = Now + 1.5;
				return false;
			}
			case 100:
				if (!Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return Waited(Now, TEXT("The player never landed."));
				}
				// Where the player really stands now (at the start it may have been standing on the CPU).
				Origin = Player->GetActorLocation();
				// Level flights stay at the thrower's height, so every check runs along a way that is open and on the
				// same floor as the player for a good distance (the player may start on a raised platform).
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(BallBuffTestOpen), false, Player);
					Params.AddIgnoredActor(CPU);
					const auto FloorAt = [&](const FVector& Where)
					{
						FHitResult Floor;
						return World->LineTraceSingleByObjectType(Floor, Where + FVector(0, 0, 50), Where - FVector(0, 0, 600),
							FCollisionObjectQueryParams(ECC_WorldStatic), Params) ? static_cast<float>(Floor.ImpactPoint.Z) : -1.0e6f;
					};
					const float Ground = FloorAt(Origin);
					float BestOpen = -1.0f;
					for (int32 Turn = 0; Turn < 24; ++Turn)
					{
						const FVector Candidate = FRotator(0.0f, Turn * 15.0f, 0.0f).Vector();
						FHitResult Blocked;
						const float Open = World->SweepSingleByObjectType(Blocked, Origin, Origin + Candidate * 1400.0f, FQuat::Identity,
							FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(90.0f), Params)
							? static_cast<float>(Blocked.Distance) : 1400.0f;
						bool bLevel = true;
						for (const float Along : {300.0f, 700.0f, 1000.0f})
						{
							bLevel &= FMath::Abs(FloorAt(Origin + Candidate * Along) - Ground) < 30.0f;
						}
						if (bLevel && Open > BestOpen)
						{
							BestOpen = Open;
							Toward = Candidate;
						}
					}
					UE_LOG(LogTemp, Display, TEXT("BALLBUFF throwing along %s (open %.0f)"), *Toward.ToCompactString(), BestOpen);
				}
				Restore(CPU, Origin - Toward * 700.0f, Toward.Rotation());
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				// A fire ball flies off with nobody in its way; beside its path, a burst's fire for comparison in the
				// screenshots (the trail fires are meant to look like a line of the flames round it).
				Throw(EChaosImpactBallType::Fire, Toward);
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Fire, Origin + Toward * 450.0f + Side * 420.0f, Player, nullptr);
				Stage = 1;
				NextAt = Now + 0.8;
				return false;
			case 1:
			{
				CaptureBallBuff(TEXT("BB-01-FireTrail.png"));
				const TArray<AChaosImpactHazardZone*> Fires = TrailFires();
				UE_LOG(LogTemp, Display, TEXT("BALLBUFF fire trail: %d fires"), Fires.Num());
				Test->TestTrue(TEXT("A flying fire ball leaves fires along its way"), Fires.Num() >= 2);
				if (Fires.IsEmpty())
				{
					return true;
				}
				// Put the CPU in the middle of one of them.
				const FVector FireLocation = Fires[Fires.Num() / 2]->GetActorLocation();
				PlaceAt(CPU, FireLocation + FVector(0.0f, 0.0f, 100.0f));
				CPUHealth = CPU->GetHealth();
				++Stage;
				NextAt = Now + 1.3;
				return false;
			}
			case 2:
				UE_LOG(LogTemp, Display, TEXT("BALLBUFF cpu in the trail: health %.0f -> %.0f"), CPUHealth, CPU->GetHealth());
				Test->TestTrue(TEXT("Standing in the trail burns"), CPU->IsEliminated() || CPU->GetHealth() < CPUHealth);
				CaptureBallBuff(TEXT("BB-02-FireTrail-Burn.png"));
				// Let the trail die out before the next checks.
				Restore(CPU, Origin - Toward * 700.0f, Toward.Rotation());
				++Stage;
				NextAt = Now + AChaosImpactHazardZone::FireTrailSeconds + 0.4;
				return false;
			case 3:
			{
				Restore(CPU, Origin + Toward.RotateAngleAxis(7.0f, FVector::UpVector) * 800.0f, Toward.Rotation());
				CPUHealth = CPU->GetHealth();
				UE_LOG(LogTemp, Display, TEXT("BALLBUFF homing: player at %s (origin %s), target placed at %s (wanted %s), eliminated %d"),
					*Player->GetActorLocation().ToCompactString(), *Origin.ToCompactString(), *CPU->GetActorLocation().ToCompactString(),
					*(Origin + Toward.RotateAngleAxis(7.0f, FVector::UpVector) * 800.0f).ToCompactString(), CPU->IsEliminated() ? 1 : 0);
				// Straight down the line (open: the fire ball flew it), 7 degrees wide of the CPU: about a metre wide
				// by the time it gets there, well clear of it without the bend.
				HomingBall = Throw(EChaosImpactBallType::Normal, Toward);
				PhaseStartedAt = Now;
				++Stage;
				NextAt = Now + 0.25;
				return false;
			}
			case 4:
				CaptureBallBuff(TEXT("BB-03-Homing.png"));
				if (const AChaosImpactBall* Ball = HomingBall.Get(); Ball && !Ball->IsPickup())
				{
					Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist(Ball->GetActorLocation(), CPU->GetActorLocation())));
				}
				if (CPU->GetHealth() >= CPUHealth && !CPU->IsEliminated() && Now - PhaseStartedAt < 1.5)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("BALLBUFF homing: cpu health %.0f -> %.0f after %.2f s, cpu at %s, ball came within %.0f"),
					CPUHealth, CPU->GetHealth(), Now - PhaseStartedAt, *CPU->GetActorLocation().ToCompactString(), Closest);
				Test->TestTrue(TEXT("A normal ball bends onto an opponent just off its line"),
					CPU->IsEliminated() || CPU->GetHealth() < CPUHealth);
				// Ice bursting 330 away (outside the old reach, inside the new one).
				Restore(CPU, Origin - Toward * 700.0f, Toward.Rotation());
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Ice,
					CPU->GetActorLocation() + Side * 330.0f - FVector(0.0f, 0.0f, 60.0f), Player, nullptr);
				Test->TestTrue(TEXT("Ice freezes 330 away"), CPU->IsIceFrozen());
				++Stage;
				NextAt = Now + 0.4;
				return false;
			case 5:
			{
				CaptureBallBuff(TEXT("BB-04-Ice.png"));
				// A tornado from the player, a ball lying in its way and the CPU well to one side.
				Restore(CPU, Origin - Toward * 700.0f - Side * 500.0f, Toward.Rotation());
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				AChaosImpactBall* Lying = SpawnBuffBall(World, EChaosImpactBallType::Normal, Origin + Toward * 420.0f + FVector(0, 0, -58), nullptr);
				Lying->MakePickup();
				LyingBall = Lying;
				// From the spot the checks were laid out around (the player may have been pushed off it by now).
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Tornado = AChaosImpactTornado::Release(World, Origin + Toward * 90.0f, Toward, Player);
				TornadoAt = Now;
				++Stage;
				NextAt = Now + 1.6;
				return false;
			}
			case 6:
			{
				const AChaosImpactTornado* Wind = Tornado.Get();
				Test->TestTrue(TEXT("The tornado swept up the lying ball"), LyingBall.IsValid() && LyingBall->IsCarriedByWind());
				if (!Wind)
				{
					Test->AddError(TEXT("The tornado is gone."));
					return true;
				}
				// A ball the CPU threw, flying into the tornado from the side. Thunder: a normal ball would bend away
				// toward the player (the CPU's nearest opponent), and the other special balls burst on the balls
				// whirling in the funnel before it can turn them.
				// Aimed where the tornado will be when the ball gets there.
				const FVector Start = Wind->GetCenter() + Side * 300.0f + FVector(0.0f, 0.0f, 70.0f);
				const FVector Meet = Wind->GetCenter() + Toward * AChaosImpactTornado::TravelSpeed * (300.0f / ChaosImpactBallTypes::ThunderSpeed);
				if (AChaosImpactBall* Ball = SpawnBuffBall(World, EChaosImpactBallType::Thunder, Start, CPU))
				{
					Ball->Launch((Meet - Start).GetSafeNormal2D(), 1500.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					CPUBall = Ball;
				}
				++Stage;
				NextAt = Now + 0.4;
				return false;
			}
			case 7:
				CaptureBallBuff(TEXT("BB-05-Tornado.png"));
				if (const AChaosImpactBall* Ball = CPUBall.Get())
				{
					UE_LOG(LogTemp, Display, TEXT("BALLBUFF cpu ball: reflections %d, player's %d, cpu's %d"), Ball->GetReflectionCount(),
						Ball->WasThrownBy(Player) ? 1 : 0, Ball->WasThrownBy(CPU) ? 1 : 0);
					if (Ball->GetReflectionCount() > 0)
					{
						Test->TestTrue(TEXT("A ball the tornado turned is its owner's now"), Ball->WasThrownBy(Player) && !Ball->WasThrownBy(CPU));
					}
					else
					{
						Test->AddWarning(TEXT("The CPU's ball missed the tornado; the takeover was not checked."));
					}
				}
				++Stage;
				return false;
			case 8:
				// When the tornado lets go, the ball it carried flies off as the player's throw.
				if (LyingBall.IsValid() && LyingBall->IsCarriedByWind())
				{
					if (Now - TornadoAt < AChaosImpactTornado::ActiveSeconds + AChaosImpactTornado::CollapseSeconds + 2.0)
					{
						return false;
					}
					Test->AddError(TEXT("The tornado never let go of the ball."));
					return true;
				}
				if (const AChaosImpactBall* Ball = LyingBall.Get())
				{
					UE_LOG(LogTemp, Display, TEXT("BALLBUFF released ball: pickup %d, velocity %s, player's %d"), Ball->IsPickup() ? 1 : 0,
						*Ball->GetBallVelocity().ToCompactString(), Ball->WasThrownBy(Player) ? 1 : 0);
					Test->TestFalse(TEXT("A ball the tornado lets go flies off"), Ball->IsPickup());
					Test->TestTrue(TEXT("It flies as the tornado owner's throw"), Ball->WasThrownBy(Player));
				}
				else
				{
					Test->AddError(TEXT("The swept-up ball vanished."));
				}
				CaptureBallBuff(TEXT("BB-06-Fling.png"));
				++Stage;
				NextAt = Now + 0.8;
				return false;
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now, const TCHAR* Error)
		{
			if (Now - StartedAt < 60.0)
			{
				return false;
			}
			Test->AddError(Error);
			return true;
		}

		FAutomationTestBase* Test;
		FVector Toward = FVector(1.0f, 0.35f, 0.0f).GetSafeNormal();
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double PhaseStartedAt = 0.0;
		double TornadoAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		float CPUHealth = 0.0f;
		float Closest = TNumericLimits<float>::Max();
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
		TWeakObjectPtr<AChaosImpactBall> HomingBall;
		TWeakObjectPtr<AChaosImpactBall> LyingBall;
		TWeakObjectPtr<AChaosImpactBall> CPUBall;
		TWeakObjectPtr<AChaosImpactTornado> Tornado;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBallBuffTest, "ChaosImpact.Training.BallBuffs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactBallBuffTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBallBuffCommand(this));
	return true;
}

#endif

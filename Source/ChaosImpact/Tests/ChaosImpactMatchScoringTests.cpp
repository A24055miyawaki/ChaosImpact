#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
	AChaosImpactPlayerController* FindScoringTestController()
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

	/**
	 * Every way of hurting an opponent in a VS match scores: each point of damage is a hit point, and a knockout adds
	 * its bonus. Balls of every type, blasts, burning ground and a thrower knocked out while the ball is in the air.
	 * Run in ?CITraining=1?CIVersus=1?CIMatch=1?CIMatchCPU=3 (free-for-all).
	 */
	class FMatchScoringCommand : public IAutomationLatentCommand
	{
	public:
		explicit FMatchScoringCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindScoringTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Match || !Player || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match
				|| Match->GetPhaseElapsedSeconds() < 0.3f)
			{
				if (Now - StartedAt < 90.0)
				{
					return false;
				}
				Test->AddError(TEXT("Run with ?CITraining=1?CIVersus=1?CIMatch=1?CIMatchCPU=3 so a VS match starts."));
				return true;
			}
			const AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>();
			if (!Own)
			{
				return false;
			}

			if (Stage == 0)
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					if (AChaosImpactCharacter* CPU = Cast<AChaosImpactCharacter>(It->GetPawn()))
					{
						Opponents.Add(CPU);
					}
				}
				if (Opponents.Num() < 3)
				{
					Test->AddError(TEXT("Three CPU opponents are needed."));
					return true;
				}
				Origin = Player->GetActorLocation();
				Facing = Player->GetActorRotation().Yaw;
				Stage = 1;
			}

			// Each case: set up, wait, then compare the points against the damage the opponents took.
			const auto Target = [this](const int32 Index) { return Opponents[Index].Get(); };
			if (!Target(0) || !Target(1) || !Target(2))
			{
				Test->AddError(TEXT("A CPU opponent disappeared."));
				return true;
			}
			const auto Begin = [&](const TCHAR* Name)
			{
				CaseName = Name;
				Player->ResetForOnlineMatch(Origin, Player->GetActorRotation());
				for (int32 Index = 0; Index < 3; ++Index)
				{
					// Spread in front of the player, far enough apart that one blast reaches only one of them.
					const FVector Side = FRotator(0.0f, Facing - 35.0f + Index * 35.0f, 0.0f).Vector();
					Target(Index)->ResetForOnlineMatch(Origin + Side * 600.0f, FRotator::ZeroRotator);
				}
				PointsBefore = Own->Points;
				KnockoutsBefore = Own->Knockouts;
				HealthBefore = 0.0f;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					HealthBefore += Target(Index)->GetHealth();
				}
			};
			const auto Check = [&]()
			{
				float HealthNow = 0.0f;
				int32 Knockouts = 0;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					HealthNow += Target(Index)->IsEliminated() ? 0.0f : Target(Index)->GetHealth();
				}
				Knockouts = Own->Knockouts - KnockoutsBefore;
				const int32 Damage = FMath::RoundToInt(HealthBefore - HealthNow);
				const int32 Gained = Own->Points - PointsBefore;
				Test->AddInfo(FString::Printf(TEXT("%s: damage %d, knockouts %d, points %d"), *CaseName, Damage, Knockouts, Gained));
				Test->TestTrue(FString::Printf(TEXT("%s hurts an opponent"), *CaseName), Damage > 0);
				Test->TestEqual(FString::Printf(TEXT("%s scores every point of damage plus knockouts"), *CaseName),
					Gained, Damage * ChaosImpactMatch::HitPoints + Knockouts * ChaosImpactMatch::KnockoutBonusPoints);
			};
			const auto Throw = [&](const EChaosImpactBallType Type, AChaosImpactCharacter* Victim, APawn* Thrower)
			{
				const FVector From = Thrower->GetActorLocation();
				const FVector Direction = (Victim->GetActorLocation() - From).GetSafeNormal2D();
				const FTransform SpawnTransform(FRotator::ZeroRotator, From + Direction * 90.0f + FVector(0.0f, 0.0f, 30.0f));
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					SpawnTransform, Thrower, Thrower, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Ball)
				{
					Ball->SetBallType(Type);
					Ball->FinishSpawning(SpawnTransform);
					Ball->Launch(Direction, 2100.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
			};

			switch (Stage)
			{
			case 1:
				Begin(TEXT("Normal ball"));
				Throw(EChaosImpactBallType::Normal, Target(0), Player);
				Stage = 2;
				NextAt = Now + 0.8;
				return false;
			case 2:
				Check();
				Begin(TEXT("Fire ball hit and burning"));
				Throw(EChaosImpactBallType::Fire, Target(1), Player);
				Stage = 3;
				NextAt = Now + 0.6;
				return false;
			case 3:
				// Keep the victim standing in the fire for a burn.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					if (It->GetZoneType() == EChaosImpactBallType::Fire && !Target(1)->IsEliminated())
					{
						Target(1)->SetActorLocation(It->GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), false, nullptr,
							ETeleportType::TeleportPhysics);
						Target(1)->ApplyIceFreeze(1.5f);
					}
				}
				Stage = 4;
				NextAt = Now + 1.3;
				return false;
			case 4:
				Check();
				ClearZones(World);
				Begin(TEXT("Fire blast"));
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Fire,
					Target(2)->GetActorLocation() + FVector(60.0f, 0.0f, 0.0f), Player, nullptr);
				Stage = 5;
				NextAt = Now + 0.4;
				return false;
			case 5:
				Check();
				ClearZones(World);
				Begin(TEXT("Thunder ball"));
				Throw(EChaosImpactBallType::Thunder, Target(0), Player);
				Stage = 6;
				NextAt = Now + 0.9;
				return false;
			case 6:
				Check();
				ClearZones(World);
				Begin(TEXT("Ice ball"));
				Throw(EChaosImpactBallType::Ice, Target(1), Player);
				Stage = 7;
				NextAt = Now + 0.8;
				return false;
			case 7:
				Check();
				ClearZones(World);
				Begin(TEXT("Knockout"));
				Target(2)->ResetForOnlineMatch(Target(2)->GetActorLocation(), FRotator::ZeroRotator);
				Throw(EChaosImpactBallType::Normal, Target(2), Player);
				Stage = 8;
				NextAt = Now + 0.8;
				return false;
			case 8:
				Throw(EChaosImpactBallType::Normal, Target(2), Player);
				Stage = 9;
				NextAt = Now + 0.8;
				return false;
			case 9:
				Throw(EChaosImpactBallType::Normal, Target(2), Player);
				Stage = 10;
				NextAt = Now + 0.8;
				return false;
			case 10:
				Test->TestTrue(TEXT("Three hits knock out"), Target(2)->IsEliminated());
				Check();
				Begin(TEXT("Thrower knocked out while the ball flies"));
				Throw(EChaosImpactBallType::Normal, Target(0), Player);
				{
					FDamageEvent Event;
					Player->TakeDamage(999.0f, Event, nullptr, nullptr);
				}
				Stage = 11;
				NextAt = Now + 0.8;
				return false;
			case 11:
				Check();
				Stage = 12;
				return false;
			default:
				return true;
			}
		}

	private:
		static void ClearZones(UWorld* World)
		{
			for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
			{
				It->Destroy();
			}
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		float Facing = 0.0f;
		FString CaseName;
		int32 PointsBefore = 0;
		int32 KnockoutsBefore = 0;
		float HealthBefore = 0.0f;
		TArray<TWeakObjectPtr<AChaosImpactCharacter>> Opponents;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMatchScoringTest, "ChaosImpact.Versus.MatchScoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactMatchScoringTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMatchScoringCommand(this));
	return true;
}

#endif

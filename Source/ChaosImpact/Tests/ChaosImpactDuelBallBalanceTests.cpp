#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactDuelBallBalance.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTornado.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

namespace
{
	/** Run in an isolated training game with one CPU. Exercises the real damage receiver and timed fields. */
	class FDuelBallBalanceCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDuelBallBalanceCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			auto* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (FPlatformTime::Seconds() - StartedAt > 100.)
			{
				Test->AddError(FString::Printf(TEXT("Duel ball balance timed out at step %d"), Step)); return true;
			}
			if (!Player || !Match || !PC->IsGameplayActive()) { return false; }
			const double Now = World->GetTimeSeconds();
			if (Now < NextAt) { return false; }
			AChaosImpactCharacter* Enemy = Target.Get();
			if (Step > 0 && !Enemy) { Test->AddError(TEXT("Missing balance test opponent")); return true; }
			const auto Clear = [World]()
			{
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { It->Destroy(); }
			};
			const auto ResetEnemy = [&]()
			{
				Enemy->SetTrainingMenuFrozen(false);
				Enemy->ResetForOnlineMatch(Origin + FVector(600, 0, 0), FRotator(0, 180, 0));
				Enemy->SetTrainingMenuFrozen(true);
			};
			const auto Ball = [&](EChaosImpactBallType Type)
			{
				const FTransform At(FRotator::ZeroRotator, Origin + FVector(0, 0, 1000));
				auto* NewBall = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At,
					Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				NewBall->SetBallType(Type); NewBall->FinishSpawning(At);
				NewBall->Launch(FVector::ForwardVector, 1.f, EChaosImpactBallFlightMode::Straight, 0.f);
				NewBall->SetActorTickEnabled(false);
				return NewBall;
			};
			switch (Step)
			{
			case 0:
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					Enemy = Cast<AChaosImpactCharacter>(It->GetPawn()); It->SetActorTickEnabled(false);
				}
				if (!Enemy || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				Target = Enemy; Origin = Player->GetActorLocation();
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					World->GetTimerManager().ClearAllTimersForObject(*It); It->Destroy();
				}
				Clear();
				World->GetTimerManager().ClearAllTimersForObject(World->GetAuthGameMode());
				Match->bVersusMatch = true; Match->Rules.bDuel = true;
				Match->Phase = EChaosImpactOnlinePhase::Match; Match->Duel.Step = EChaosImpactDuelStep::Fight;
				Player->SetTrainingMenuFrozen(true);
				// Every special kind is limited, even a future increase to its direct-hit damage.
				for (int32 Kind = 1; Kind < ChaosImpactBallTypes::Count; ++Kind)
				{
					ResetEnemy();
					auto* Shot = Ball(static_cast<EChaosImpactBallType>(Kind));
					const float Before = Enemy->GetHealth();
					Enemy->TakeDamage(2.f, FDamageEvent(), Player->GetController(), Shot);
					Enemy->TakeDamage(1.f, FDamageEvent(), Player->GetController(), Shot);
					Test->TestEqual(FString::Printf(TEXT("%s one throw costs at most 1 HP"),
						ChaosImpactBallTypes::GetInternalName(static_cast<EChaosImpactBallType>(Kind))), Before - Enemy->GetHealth(), 1.f);
					Shot->Destroy();
				}
				Clear(); ResetEnemy();
				{
					auto* Wind = AChaosImpactTornado::Release(World, Origin + FVector(-2000, 0, 100), FVector::RightVector, Player);
					auto* Redirected = Ball(EChaosImpactBallType::Normal);
					Redirected->SetDuelThrowDamage(Wind->GetDuelThrowDamage());
					Enemy->TakeDamage(1.f, FDamageEvent(), Player->GetController(), Wind);
					Enemy->TakeDamage(1.f, FDamageEvent(), Player->GetController(), Redirected);
					Test->TestEqual(TEXT("Wind and a ball redirected by the same wind share 1 HP"), Enemy->GetHealth(), 2.f);
					ResetEnemy();
					auto* Normal = Ball(EChaosImpactBallType::Normal);
					Enemy->TakeDamage(2.f, FDamageEvent(), Player->GetController(), Normal);
					Test->TestEqual(TEXT("Ordinary throws do not inherit a previous wind damage budget"), Enemy->GetHealth(), 1.f);
				}
				Clear(); ResetEnemy();
				{
					auto* Fire = Ball(EChaosImpactBallType::Fire);
					const auto Budget = Fire->GetDuelThrowDamage();
					Enemy->TakeDamage(1.f, FDamageEvent(), Player->GetController(), Fire);
					// Its explosion and multiple trail fires must all remember the direct hit, after the ball is gone.
					Zone = AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Fire,
						Enemy->GetActorLocation(), Player, Enemy, 1.f, Budget);
					auto* Trail = AChaosImpactHazardZone::SpawnFireTrail(World, Enemy->GetActorLocation(), Player, nullptr, Budget);
					Test->TestNotNull(TEXT("Trail on the test stage"), Trail);
					AChaosImpactHazardZone::SpawnFireTrail(World, Enemy->GetActorLocation(), Player, Trail, Budget);
					Fire->Destroy();
				}
				NextAt = Now + 2.2; ++Step; return false;
			case 1:
				Test->TestEqual(TEXT("Fire direct + burst + overlapping trail over two seconds = 1 HP"), Enemy->GetHealth(), 2.f);
				{
					auto* Second = Ball(EChaosImpactBallType::Fire);
					Enemy->TakeDamage(1.f, FDamageEvent(), Player->GetController(), Second);
					Test->TestEqual(TEXT("A separate throw can follow up"), Enemy->GetHealth(), 1.f);
				}
				Clear(); ResetEnemy();
				Zone = AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Black, Enemy->GetActorLocation(), Player, nullptr);
				Test->TestEqual(TEXT("Duel black radius"), Zone->GetRadius(), 500.f);
				Test->TestEqual(TEXT("Duel black duration"), Zone->GetActiveSeconds(), 2.5f);
				NextAt = Now + 1.5; ++Step; return false;
			case 2:
				Test->TestEqual(TEXT("Repeated black-hole core burns capped at 1 HP"), Enemy->GetHealth(), 2.f);
				Enemy->SetActorLocation(Zone->GetActorLocation() + FVector(470, 0, 90));
				Test->TestTrue(TEXT("Walking can overcome edge pull"), AChaosImpactHazardZone::GetBlackHolePullOffset(World, Enemy, .1f).Size() < 56.f);
				NextAt = Now + 1.3; ++Step; return false;
			case 3:
				Test->TestTrue(TEXT("No invisible pull after shortened black-hole duration"),
					AChaosImpactHazardZone::GetBlackHolePullOffset(World, Enemy, .1f).IsNearlyZero());
				Clear(); ResetEnemy();
				Enemy->SetTrainingMenuFrozen(false);
				Enemy->ApplyIceFreeze(2.f);
				Test->TestTrue(TEXT("Ice still freezes on impact"), Enemy->IsIceFrozen());
				BaseSpeed = Enemy->GetCharacterMovement()->MaxWalkSpeed;
				NextAt = Now + .5; ++Step; return false;
			case 4:
				Test->TestFalse(TEXT("Ice thawed after 0.4 seconds"), Enemy->IsIceFrozen());
				Test->TestTrue(TEXT("Short follow-up slow"), FMath::IsNearlyEqual(Enemy->GetCharacterMovement()->MaxWalkSpeed, BaseSpeed * .75f, 1.f));
				Enemy->ApplyIceFreeze(2.f);
				Test->TestFalse(TEXT("Repeated ice cannot freeze-lock during grace"), Enemy->IsIceFrozen());
				NextAt = Now + .85; ++Step; return false;
			case 5:
				Test->TestTrue(TEXT("Ice slowdown wears off"), FMath::IsNearlyEqual(Enemy->GetCharacterMovement()->MaxWalkSpeed, BaseSpeed, 1.f));
				ResetEnemy();
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				{
					int32 Count = 0;
					for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { ++Count; }
					Test->TestEqual(TEXT("Duel retains all twelve birds"), Count, 12);
				}
				NextAt = Now + 3.2; ++Step; return false;
			case 6:
				Test->TestEqual(TEXT("A real perched flock pecks only once in duel"), Enemy->GetHealth(), 2.f);
				Clear(); ResetEnemy();
				Enemy->SetTrainingMenuFrozen(false);
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				NextAt = Now + .05; ++Step; return false;
			case 7:
				if (!AChaosImpactSimaeBird::HasPerchedBird(Enemy))
				{
					if (Now > NextAt + 2.) { Test->AddError(TEXT("No flock perched for dash test")); ++Step; NextAt = Now; }
					return false;
				}
				Enemy->RequestAIDash(FVector::RightVector);
				NextAt = Now + 2.; ++Step; return false;
			case 8:
				Test->TestEqual(TEXT("Dashing shakes off the flock without damage"), Enemy->GetHealth(), 3.f);
				Clear(); ResetEnemy();
				Zone = AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke,
					Enemy->GetActorLocation() + FVector(330, 0, 0), Player, nullptr);
				Test->TestEqual(TEXT("Duel smoke radius"), Zone->GetRadius(), 260.f);
				Test->TestFalse(TEXT("Duel smoke misses someone 330 cm away"), Enemy->IsBlinded());
				Clear();
				Match->Rules.bDuel = false;
				ResetEnemy();
				Zone = AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke,
					Enemy->GetActorLocation() + FVector(330, 0, 0), Player, nullptr);
				Test->TestEqual(TEXT("Regular smoke radius unchanged"), Zone->GetRadius(), 400.f);
				Test->TestTrue(TEXT("Regular smoke still blinds at 330 cm"), Enemy->IsBlinded());
				Clear(); ResetEnemy();
				Zone = AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Black, Enemy->GetActorLocation(), Player, nullptr);
				Test->TestEqual(TEXT("Regular black radius unchanged"), Zone->GetRadius(), 760.f);
				Test->TestEqual(TEXT("Regular black duration unchanged"), Zone->GetActiveSeconds(), 4.f);
				Clear();
				Enemy->SetTrainingMenuFrozen(false); Enemy->ApplyIceFreeze(2.f);
				NextAt = Now + .55; ++Step; return false;
			case 9:
				Test->TestTrue(TEXT("Regular ice still freezes for two seconds"), Enemy->IsIceFrozen());
				ResetEnemy();
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Fire, Enemy->GetActorLocation(), Player, nullptr);
				NextAt = Now + 1.2; ++Step; return false;
			case 10:
				Test->TestTrue(TEXT("Regular fire retains damage over time"), Enemy->GetHealth() <= 1.f);
				Clear(); ResetEnemy();
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				NextAt = Now + 3.3; ++Step; return false;
			case 11:
				// Arrival time varies with the birds' initial spread. Allow time for the second scheduled peck.
				if (Enemy->GetHealth() > 1.f && Now < NextAt + 2.) { return false; }
				Test->TestEqual(TEXT("Regular flock retains two pecks"), Enemy->GetHealth(), 1.f);
				Clear(); Enemy->SetTrainingMenuFrozen(false); Player->SetTrainingMenuFrozen(false);
				Match->bVersusMatch = false;
				return true;
			}
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<AChaosImpactCharacter> Target;
		TWeakObjectPtr<AChaosImpactHazardZone> Zone;
		FVector Origin = FVector::ZeroVector;
		double StartedAt = FPlatformTime::Seconds(), NextAt = 0.;
		float BaseSpeed = 560.f;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelBallBalanceTest, "ChaosImpact.Match.DuelBallBalance",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelBallBalanceTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDuelBallBalanceCommand(this));
	return true;
}
#endif

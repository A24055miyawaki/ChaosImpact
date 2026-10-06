#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Solo mode enemies: characters that get their CPU by themselves, as one placed in a level does (or "Spawn AI From
	 * Class"), play at the strength set on them, stand together (never target or hurt each other) and go for the player;
	 * one set not to come back is gone after its knockout. Training arena with no CPUs (?CITraining=1?CICPUCount=0?CITargets=0).
	 */
	class FSoloEnemyCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSoloEnemyCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (!World || !Player || Now < NextAt)
			{
				return Waited(Now);
			}
			switch (Stage)
			{
			case 0:
			{
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return Waited(Now);
				}
				AGameModeBase* Mode = World->GetAuthGameMode();
				UClass* PawnClass = Mode ? Mode->GetDefaultPawnClassForController(PC) : nullptr;
				const FVector Toward = Player->GetActorForwardVector().GetSafeNormal2D();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
				const EChaosImpactCPULevel Levels[] = {EChaosImpactCPULevel::Strongest, EChaosImpactCPULevel::Weak};
				for (int32 Index = 0; Index < 2 && PawnClass; ++Index)
				{
					const FVector At = Player->GetActorLocation() + Toward * 700.0f + Side * (Index == 0 ? -260.0f : 260.0f);
					const FTransform Where((-Toward).Rotation(), At);
					AChaosImpactCharacter* Enemy = World->SpawnActorDeferred<AChaosImpactCharacter>(PawnClass, Where, nullptr, nullptr,
						ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
					Enemy->CPULevel = Levels[Index];
					Enemy->bRespawnAfterElimination = Index == 0;
					Enemy->FinishSpawning(Where);
					// What a character placed in a level gets by itself.
					Enemy->SpawnDefaultController();
					Enemies.Add(Enemy);
				}
				if (Enemies.Num() != 2)
				{
					Test->AddError(TEXT("Could not spawn the enemies."));
					return true;
				}
				for (int32 Index = 0; Index < 2; ++Index)
				{
					const AChaosImpactCPUController* CPU = Cast<AChaosImpactCPUController>(Enemies[Index]->GetController());
					Test->TestNotNull(TEXT("The character took the VS mode's CPU by itself"), CPU);
					Test->TestEqual(TEXT("It plays at the strength set on it"), CPU ? CPU->GetDifficulty() : -1,
						static_cast<int32>(Levels[Index]));
					Test->TestEqual(TEXT("Placed enemies join side 1"), Enemies[Index]->SoloTeam, AChaosImpactCharacter::SoloEnemyTeam);
				}
				Test->TestTrue(TEXT("The enemies are on one side"), AChaosImpactGameState::AreTeammates(World, Enemies[0].Get(), Enemies[1].Get()));
				Test->TestFalse(TEXT("The player is not on their side"), AChaosImpactGameState::AreTeammates(World, Enemies[0].Get(), Player));
				// Changed later from a Blueprint (the setter), it takes effect at once.
				Enemies[1]->SetCPULevel(EChaosImpactCPULevel::Normal);
				const AChaosImpactCPUController* Second = Cast<AChaosImpactCPUController>(Enemies[1]->GetController());
				Test->TestEqual(TEXT("Set CPU Level applies at once"), Second ? Second->GetDifficulty() : -1,
					static_cast<int32>(EChaosImpactCPULevel::Normal));
				PlayerHealth = Player->GetHealth();
				Stage = 1;
				FightUntil = Now + 25.0;
				return false;
			}
			case 1:
			{
				// The player stands still: whatever the enemies throw is at them, never each other.
				for (const TWeakObjectPtr<AChaosImpactCharacter>& Enemy : Enemies)
				{
					if (!Enemy.IsValid() || Enemy->GetHealth() < Enemy->GetMaxHealth() || Enemy->IsEliminated())
					{
						Test->AddError(TEXT("An enemy was hurt by the other."));
						return true;
					}
				}
				PlayerTaken += Player->GetHealth() < PlayerHealth ? 1 : 0;
				PlayerTaken += Player->IsEliminated() && !bPlayerWasDown ? 1 : 0;
				bPlayerWasDown = Player->IsEliminated();
				PlayerHealth = Player->GetHealth();
				if (Now < FightUntil)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("SOLOENEMY 25 s: the player was hit %d times, the enemies never each other"), PlayerTaken);
				Test->TestTrue(TEXT("The enemies go for the player"), PlayerTaken > 0);
				// Knock out the one set not to come back.
				AChaosImpactCharacter* Beaten = Enemies[1].Get();
				Removed = Beaten->GetController();
				Beaten->TakeDamage(Beaten->GetMaxHealth(), FDamageEvent(), PC, Player);
				Test->TestTrue(TEXT("Knocked out"), Beaten->IsEliminated());
				Stage = 2;
				NextAt = Now + 3.0;
				return false;
			}
			case 2:
				Test->TestFalse(TEXT("One set not to come back is gone after its knockout"), Enemies[1].IsValid());
				Test->TestFalse(TEXT("...with its CPU"), Removed.IsValid());
				Test->TestTrue(TEXT("The other one is still here"), Enemies[0].IsValid());
				return true;
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now)
		{
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(TEXT("The training arena never got ready."));
				return true;
			}
			return false;
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double FightUntil = 0.0;
		int32 Stage = 0;
		float PlayerHealth = 0.0f;
		int32 PlayerTaken = 0;
		bool bPlayerWasDown = false;
		TArray<TWeakObjectPtr<AChaosImpactCharacter>> Enemies;
		TWeakObjectPtr<AController> Removed;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSoloEnemyTest, "ChaosImpact.Training.SoloEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSoloEnemyTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloEnemyCommand(this));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * One さいきょう CPU against seven つよい CPUs on one team, on a VS level the player only watches:
	 * ?CITraining=1?CIVersus=1?CIMatch=1?CIMinutes=5?CITeams=0?CIMatchCPU=8?CISpectate=1?CILocalPlayers=1?CIKeyboardPlayer=0
	 * (a free-for-all, turned into two teams here once it runs: a team battle would stop at team select first).
	 * For 150 seconds its knockouts and its own are counted, with every throw and hit either way.
	 */
	class FCPUGauntletCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUGauntletCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				World = Context.World() && Context.World()->IsGameWorld() ? Context.World() : World;
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match || Match->IsMatchInputLocked())
			{
				if (StartedAt > 0.0 || Now - CreatedAt > 150.0)
				{
					Test->AddError(TEXT("The match was not running."));
					return true;
				}
				return false;
			}
			if (StartedAt <= 0.0)
			{
				TArray<AChaosImpactCPUController*> CPUs;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUs.Add(*It);
				}
				if (CPUs.Num() < 4)
				{
					Test->AddError(FString::Printf(TEXT("Expected 8 CPUs, found %d."), CPUs.Num()));
					return true;
				}
				// Two teams: the first alone at さいきょう, the rest together at つよい.
				const_cast<AChaosImpactGameState*>(Match)->Rules.TeamCount = 2;
				for (int32 Index = 0; Index < CPUs.Num(); ++Index)
				{
					CPUs[Index]->SetDifficulty(Index == 0 ? ChaosImpactMatch::CPULevelStrongest : ChaosImpactMatch::CPULevelStrong);
					if (AChaosImpactPlayerState* State = CPUs[Index]->GetPlayerState<AChaosImpactPlayerState>())
					{
						State->TeamIndex = Index == 0 ? 0 : 1;
					}
					if (Index > 0)
					{
						Others.Add(Cast<AChaosImpactCharacter>(CPUs[Index]->GetPawn()));
					}
				}
				Lone = Cast<AChaosImpactCharacter>(CPUs[0]->GetPawn());
				OthersCount = CPUs.Num() - 1;
				StartedAt = Now;
			}
			if (!Lone.IsValid())
			{
				Test->AddError(TEXT("The さいきょう CPU went missing."));
				return true;
			}
			// Knockouts: the lone one going down, and any of the others going down (only it can hurt them).
			const bool bLoneDown = Lone->IsEliminated();
			if (bLoneDown && !bLoneWasDown)
			{
				++LoneDowns;
			}
			bLoneWasDown = bLoneDown;
			CountHealth(Lone.Get(), LoneHealth, LoneTaken);
			for (int32 Index = 0; Index < Others.Num(); ++Index)
			{
				if (!Others[Index].IsValid())
				{
					continue;
				}
				const bool bDown = Others[Index]->IsEliminated();
				if (OthersWasDown.Num() <= Index)
				{
					OthersWasDown.Add(false);
					OthersHealth.Add(-1.0f);
				}
				if (bDown && !OthersWasDown[Index])
				{
					++Knockouts;
				}
				OthersWasDown[Index] = bDown;
				CountHealth(Others[Index].Get(), OthersHealth[Index], Dealt);
			}
			for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
			{
				if (It->IsPickup())
				{
					Seen.Remove(*It);
				}
				else if (!Seen.Contains(*It))
				{
					Seen.Add(*It);
					if (It->WasThrownBy(Lone.Get()))
					{
						++LoneThrows;
					}
					else if (It->GetThrowingPawn())
					{
						++OthersThrows;
					}
				}
			}
			if (Now - StartedAt < 150.0)
			{
				return false;
			}
			UE_LOG(LogTemp, Display, TEXT("CPUGAUNTLET 1 さいきょう vs %d つよい: knockouts %d, went down %d | throws %d hits %d (%.0f%%) | took %d of %d throws (dodged %.0f%%)"),
				OthersCount, Knockouts, LoneDowns, LoneThrows, Dealt, LoneThrows > 0 ? 100.0f * Dealt / LoneThrows : 0.0f,
				LoneTaken, OthersThrows, OthersThrows > 0 ? 100.0f * (1.0f - static_cast<float>(LoneTaken) / OthersThrows) : 100.0f);
			Test->TestTrue(TEXT("さいきょう knocks out more than it goes down, alone against seven"), Knockouts > LoneDowns);
			return true;
		}

	private:
		static void CountHealth(const AChaosImpactCharacter* Character, float& LastHealth, int32& Taken)
		{
			if (!Character || Character->IsEliminated())
			{
				LastHealth = -1.0f;
				return;
			}
			if (LastHealth >= 0.0f && Character->GetHealth() < LastHealth)
			{
				Taken += FMath::RoundToInt(LastHealth - Character->GetHealth());
			}
			LastHealth = Character->GetHealth();
		}

		FAutomationTestBase* Test;
		double CreatedAt = FPlatformTime::Seconds();
		double StartedAt = 0.0;
		TWeakObjectPtr<AChaosImpactCharacter> Lone;
		TArray<TWeakObjectPtr<AChaosImpactCharacter>> Others;
		TArray<bool> OthersWasDown;
		TArray<float> OthersHealth;
		int32 OthersCount = 0;
		bool bLoneWasDown = false;
		int32 LoneDowns = 0;
		int32 Knockouts = 0;
		float LoneHealth = -1.0f;
		int32 LoneTaken = 0;
		int32 Dealt = 0;
		int32 LoneThrows = 0;
		int32 OthersThrows = 0;
		TSet<TWeakObjectPtr<AChaosImpactBall>> Seen;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUGauntletTest, "ChaosImpact.Versus.CPUGauntlet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUGauntletTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUGauntletCommand(this));
	return true;
}

#endif

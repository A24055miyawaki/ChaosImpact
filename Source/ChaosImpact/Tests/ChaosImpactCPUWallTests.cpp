#if WITH_DEV_AUTOMATION_TESTS
// CPUs against walls (2026-11-16): two CPUs fight a duel on a 1v1 stage for 40 seconds and each one's "jitter" is
// measured: a second in which it walked a long way but got nowhere (rocking left and right in front of a wall). Run on
// the duel's level: Lvl_VS_Duel?CITraining=1?CITargets=0?CIVersus=1?CIMatch=1?CIMatchCPU=0?CIDuel=1?CIStage=N
// (-CICPULevel=n is not needed: the duel's CPU is さいきょう; the player's side gets a CPU of -CIWallLevel=n, default 3).
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	class FCPUWallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUWallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Stalled at step %d"), Step));
				return true;
			}
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				return false;
			}
			if (Step == 0)
			{
				// カオスアリーナ: the low wall before the red start is seen as one to jump, from just before it; a pillar is not.
				if (Match->IsDuel() && Match->Rules.StageIndex == 0)
				{
					AChaosImpactCPUController* Probe = nullptr;
					for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { Probe = *It; break; }
					if (Probe)
					{
						const FVector Centre = Match->StageCenter;
						const float Middle = 96.0f;
						const FVector BeforeWall = Centre + FVector(-1180.0f, 0.0f, Middle);
						const bool bLow = Probe->HasLowObstacleAheadForTests(BeforeWall, FVector(1.0f, 0.0f, 0.0f));
						const bool bPillar = Probe->HasLowObstacleAheadForTests(Centre + FVector(-800.0f, 640.0f, Middle), FVector(1.0f, 0.0f, 0.0f));
						UE_LOG(LogTemp, Display, TEXT("CPUWALL low wall seen %d, pillar seen as low %d"), bLow ? 1 : 0, bPillar ? 1 : 0);
						Test->TestTrue(TEXT("The low wall (jumpable) is seen ahead"), bLow);
						Test->TestFalse(TEXT("A pillar is not taken for a low wall"), bPillar);
					}
				}
				// The player's side played by a CPU too (a duel; a VS match is watched, CPUs only).
				if (APawn* Mine = Match->IsDuel() ? PC->GetPawn() : nullptr)
				{
					PC->UnPossess();
					FActorSpawnParameters Parameters;
					Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					if (AChaosImpactCPUController* Stand = World->SpawnActor<AChaosImpactCPUController>(AChaosImpactCPUController::StaticClass(),
						Mine->GetActorLocation(), Mine->GetActorRotation(), Parameters))
					{
						Stand->Possess(Mine);
					}
				}
				Step = 1;
				SampleAt = Now + 3.0;
				EndAt = Now + 43.0;
				return false;
			}
			if (Now < SampleAt)
			{
				return false;
			}
			SampleAt = Now + 0.1;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (It->IsEliminated())
				{
					continue;
				}
				TArray<FVector>& Path = Paths.FindOrAdd(*It);
				Path.Add(It->GetActorLocation());
			}
			if (Now < EndAt)
			{
				return false;
			}
			// Each second (10 samples): the way walked against the ground gained.
			for (const TPair<TWeakObjectPtr<AChaosImpactCharacter>, TArray<FVector>>& Entry : Paths)
			{
				const TArray<FVector>& Path = Entry.Value;
				int32 Jitter = 0;
				int32 Windows = 0;
				for (int32 Start = 0; Start + 10 < Path.Num(); Start += 10)
				{
					float Walked = 0.0f;
					for (int32 Index = Start + 1; Index <= Start + 10; ++Index)
					{
						Walked += FVector::Dist2D(Path[Index], Path[Index - 1]);
					}
					const float Gained = FVector::Dist2D(Path[Start + 10], Path[Start]);
					Jitter += Walked > 150.0f && Gained < 50.0f ? 1 : 0;
					++Windows;
				}
				UE_LOG(LogTemp, Display, TEXT("CPUWALL stage %d %s: rocking in place %d of %d seconds"), Match->Rules.StageIndex,
					*GetNameSafe(Entry.Key.Get()), Jitter, Windows);
				Test->TestTrue(FString::Printf(TEXT("%s does not rock in front of walls (%d of %d seconds)"), *GetNameSafe(Entry.Key.Get()), Jitter, Windows),
					Jitter <= 3);
			}
			return true;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double SampleAt = 0.0;
		double EndAt = 0.0;
		int32 Step = 0;
		TMap<TWeakObjectPtr<AChaosImpactCharacter>, TArray<FVector>> Paths;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUWallTest, "ChaosImpact.Versus.CPUWalls",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactCPUWallTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUWallCommand(this));
	return true;
}
#endif

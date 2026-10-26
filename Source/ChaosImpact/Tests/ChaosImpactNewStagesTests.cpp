#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCyberStage.h"
#include "ChaosImpactForgeStage.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSnowStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
	AChaosImpactPlayerController* FindNewStagesTestController()
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

	void VerifyStageFloorAndPoints(FAutomationTestBase* Test, UWorld* World, AChaosImpactStageBase* Stage, const TCHAR* StageLabel)
	{
		Test->TestNotNull(FString::Printf(TEXT("%s exists"), StageLabel), Stage);
		if (!Stage || !World)
		{
			return;
		}
		const TArray<FVector> Spawns = Stage->GetSpawnPoints();
		const TArray<FVector> Balls = Stage->GetBallPoints();
		Test->TestEqual(FString::Printf(TEXT("%s has 12 spawn points"), StageLabel), Spawns.Num(), 12);
		Test->TestEqual(FString::Printf(TEXT("%s has 9 ball points"), StageLabel), Balls.Num(), 9);

		for (int32 Index = 0; Index < Spawns.Num(); ++Index)
		{
			FHitResult Hit;
			const bool bFloor = World->LineTraceSingleByChannel(
				Hit, Spawns[Index] + FVector(0.0f, 0.0f, 180.0f), Spawns[Index] - FVector(0.0f, 0.0f, 180.0f), ECC_WorldStatic);
			Test->TestTrue(FString::Printf(TEXT("%s spawn %d has floor"), StageLabel, Index), bFloor);
			Test->TestFalse(FString::Printf(TEXT("%s spawn %d is inside stage"), StageLabel, Index),
				Stage->IsOutside(Spawns[Index] + FVector(0.0f, 0.0f, 90.0f)));
		}
		for (int32 Index = 0; Index < Balls.Num(); ++Index)
		{
			FHitResult Hit;
			const bool bFloor = World->LineTraceSingleByChannel(
				Hit, Balls[Index] + FVector(0.0f, 0.0f, 180.0f), Balls[Index] - FVector(0.0f, 0.0f, 180.0f), ECC_WorldStatic);
			Test->TestTrue(FString::Printf(TEXT("%s ball pad %d has floor"), StageLabel, Index), bFloor);
		}
	}

	class FNewStagesGimmickCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNewStagesGimmickCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindNewStagesTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactGameMode* Mode = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			if (!PC || !World || !Mode)
			{
				return Now - StartedAt > 120.0;
			}
			const auto Wait = [this, Now](const double Seconds)
			{
				++Step;
				NextAt = Now + Seconds;
				return false;
			};

			switch (Step)
			{
			case 0:
			{
				if (Now - StartedAt < 2.5)
				{
					return false;
				}
				// ---- Stage 4: Cyber Stage -------------------------------------------------------------------
				FChaosImpactMatchRules Rules;
				Rules.StageIndex = 3;
				Rules.CPUCount = 1;
				Rules.TeamCount = 0;
				Mode->ConfigureVersusMatch(Rules);
				return Wait(0.5);
			}
			case 1:
			{
				for (TActorIterator<AChaosImpactCyberStage> It(World); It; ++It) { CyberStage = *It; }
				VerifyStageFloorAndPoints(Test, World, CyberStage.Get(), TEXT("Stage 4 (Cyber)"));
				if (!CyberStage.IsValid())
				{
					return true;
				}
				if (AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>())
				{
					Match->Phase = EChaosImpactOnlinePhase::Match;
				}
				CyberStage->GateWarnSeconds = 0.4f;
				CyberStage->GateActiveSeconds = 3.0f;
				CyberStage->StartGatePattern(0);
				Test->TestTrue(TEXT("Cyber gate warning started"), CyberStage->GetGateProgress() < 1.0f);
				return Wait(0.6);
			}
			case 2:
			{
				Test->TestTrue(TEXT("Cyber gate is now active"), CyberStage->GetGateProgress() >= 1.0f);
				// Verify Stage 5: Snow Stage
				FChaosImpactMatchRules Rules;
				Rules.StageIndex = 4;
				Rules.CPUCount = 1;
				Rules.TeamCount = 0;
				Mode->ConfigureVersusMatch(Rules);
				return Wait(0.5);
			}
			case 3:
			{
				for (TActorIterator<AChaosImpactSnowStage> It(World); It; ++It) { SnowStage = *It; }
				VerifyStageFloorAndPoints(Test, World, SnowStage.Get(), TEXT("Stage 5 (Snow)"));
				if (!SnowStage.IsValid())
				{
					return true;
				}
				// Verify the diagonal CornerPondRamp has solid floor at its midpoint
				const FVector DiagMid = SnowStage->GetActorTransform().TransformPosition(
					FVector(1.0f, 1.0f, 0.0f).GetSafeNormal() * 1895.0f);
				FHitResult RampHit;
				Test->TestTrue(TEXT("Snow Stage CornerPondRamp has solid floor"),
					World->LineTraceSingleByChannel(RampHit, DiagMid + FVector(0.0f, 0.0f, 150.0f), DiagMid - FVector(0.0f, 0.0f, 150.0f), ECC_WorldStatic));

				if (AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>())
				{
					Match->Phase = EChaosImpactOnlinePhase::Match;
				}
				SnowStage->WindWarnSeconds = 0.4f;
				SnowStage->WindActiveSeconds = 3.0f;
				SnowStage->StartWindGust(1);
				Test->TestTrue(TEXT("Snow wind warning started"), SnowStage->GetWindProgress() < 1.0f);
				return Wait(0.6);
			}
			case 4:
			{
				Test->TestTrue(TEXT("Snow wind gust is now active"), SnowStage->GetWindProgress() >= 1.0f);
				// Verify Stage 6: Forge Stage
				FChaosImpactMatchRules Rules;
				Rules.StageIndex = 5;
				Rules.CPUCount = 1;
				Rules.TeamCount = 0;
				Mode->ConfigureVersusMatch(Rules);
				return Wait(1.2);
			}
			case 5:
			{
				for (TActorIterator<AChaosImpactForgeStage> It(World); It; ++It) { ForgeStage = *It; }
				VerifyStageFloorAndPoints(Test, World, ForgeStage.Get(), TEXT("Stage 6 (Forge)"));
				if (!ForgeStage.IsValid())
				{
					return true;
				}
				// Verify the diagonal DiagCoreBridge has solid floor at its midpoint over the magma sea
				const FVector BridgeMid = ForgeStage->GetActorTransform().TransformPosition(
					FVector(1.0f, 1.0f, 0.0f).GetSafeNormal() * 2150.0f + FVector(0.0f, 0.0f, 30.0f));
				FHitResult BridgeHit;
				Test->TestTrue(TEXT("Forge Stage DiagCoreBridge has solid floor"),
					World->LineTraceSingleByChannel(BridgeHit, BridgeMid + FVector(0.0f, 0.0f, 150.0f), BridgeMid - FVector(0.0f, 0.0f, 150.0f), ECC_WorldStatic));

				// Verify the central Crucible Altar spawner only spawns special balls (Normal weight == 0)
				AChaosImpactBallSpawner* CrucibleSpawner = nullptr;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					if (FVector::Dist2D(It->GetActorLocation(), ForgeStage->GetActorLocation()) < 220.0f)
					{
						CrucibleSpawner = *It;
					}
				}
				Test->TestNotNull(TEXT("Forge Stage Crucible ball spawner exists"), CrucibleSpawner);
				if (CrucibleSpawner)
				{
					Test->TestEqual(TEXT("Crucible spawner disables Normal balls"),
						CrucibleSpawner->GetChance(EChaosImpactBallType::Normal), 0.0f);
				}

				if (AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>())
				{
					Match->Phase = EChaosImpactOnlinePhase::Match;
				}
				ForgeStage->VentWarnSeconds = 0.4f;
				ForgeStage->VentEruptSeconds = 2.0f;
				ForgeStage->StartVentPair(0);
				Test->TestTrue(TEXT("Forge thermal vent warning started"), ForgeStage->GetVentProgress() < 1.0f);
				return Wait(0.6);
			}
			case 6:
			{
				Test->TestTrue(TEXT("Forge thermal vent is erupting"), ForgeStage->GetVentProgress() >= 1.0f);
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<AChaosImpactCyberStage> CyberStage;
		TWeakObjectPtr<AChaosImpactSnowStage> SnowStage;
		TWeakObjectPtr<AChaosImpactForgeStage> ForgeStage;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNewStagesTest, "ChaosImpact.Versus.NewStages4To6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactNewStagesTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNewStagesGimmickCommand(this));
	return true;
}

#endif

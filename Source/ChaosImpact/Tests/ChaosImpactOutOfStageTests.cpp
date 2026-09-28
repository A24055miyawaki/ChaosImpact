#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactStageBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindOutOfStageController()
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
	 * Run on a local VS level (?CITraining=1?CITargets=0?CIVersus=1?CIMatch=1?CIMatchCPU=1?CIStage=N). The stage's
	 * own points are inside it; a player put past the wall and a CPU put under the floor are knocked out and come
	 * back on the stage. Screenshots go to Saved/OutOfStage.
	 */
	class FOutOfStageCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOutOfStageCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindOutOfStageController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const AChaosImpactGameMode* Mode = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			const bool bPlaying = Match && Mode && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match
				&& !Match->IsMatchInputLocked();
			if (!bPlaying)
			{
				if (Now - StartedAt < 150.0)
				{
					return false;
				}
				Test->AddError(FString::Printf(TEXT("The match was not running at step %d."), Stage));
				return true;
			}
			AChaosImpactCharacter* Player = Cast<AChaosImpactCharacter>(PC->GetPawn());
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				CPU = *It != Player ? *It : CPU;
			}
			if (!Player || !CPU)
			{
				Test->AddError(TEXT("Need the player and a CPU."));
				return true;
			}
			const FVector Center = Match->StageCenter;
			const float Reach = Match->StageHalfExtent;
			const auto Put = [](AChaosImpactCharacter* Character, const FVector& Where)
			{
				Character->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
			};

			switch (Stage)
			{
			case 0:
			{
				const AChaosImpactStageBase* StageActor = nullptr;
				for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It) { StageActor = *It; }
				if (!StageActor)
				{
					Test->AddError(TEXT("No stage."));
					return true;
				}
				UE_LOG(LogTemp, Display, TEXT("OUTOFSTAGE stage %s half %.0f"), *StageActor->GetClass()->GetName(), Reach);
				// Everything the stage offers is inside it, with a character standing there (its middle 96 up).
				for (const FVector& Point : StageActor->GetSpawnPoints())
				{
					Test->TestFalse(FString::Printf(TEXT("Spawn %s is on the stage"), *Point.ToString()),
						Mode->IsOutsideStage(Point + FVector(0, 0, 96)));
				}
				for (const FVector& Point : StageActor->GetBallPoints())
				{
					Test->TestFalse(FString::Printf(TEXT("Ball pad %s is on the stage"), *Point.ToString()),
						Mode->IsOutsideStage(Point + FVector(0, 0, 96)));
				}
				Test->TestFalse(TEXT("High above the middle is on the stage"), Mode->IsOutsideStage(Center + FVector(0, 0, 900)));
				Test->TestTrue(TEXT("Past the side wall is off the stage"), Mode->IsOutsideStage(Center + FVector(Reach + 800, 0, 96)));
				Test->TestTrue(TEXT("Under the floor is off the stage"), Mode->IsOutsideStage(Center + FVector(0, 0, -900)));
				Test->TestFalse(TEXT("Nobody is out yet"), Player->IsEliminated() || CPU->IsEliminated());

				// The player lands past the wall; a CPU drops through the floor.
				Put(Player, Center + FVector(Reach + 800.0f, 0.0f, 150.0f));
				Put(CPU, Center + FVector(0.0f, 0.0f, -1200.0f));
				++Stage;
				NextAt = Now + 1.0;
				return false;
			}
			case 1:
				Test->TestTrue(TEXT("The player past the wall is knocked out"), Player->IsEliminated());
				Test->TestTrue(TEXT("The CPU under the floor is knocked out"), CPU->IsEliminated());
				++Stage;
				NextAt = Now + 1.5;
				return false;
			case 2:
				// The respawn panel, which names no one: ステージの外に出た！
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OutOfStage"), TEXT("01-Knocked.png")),
					true, false);
				++Stage;
				NextAt = Now + 5.0;
				return false;
			case 3:
				Test->TestFalse(TEXT("The player is back"), Player->IsEliminated());
				Test->TestFalse(TEXT("The CPU is back"), CPU->IsEliminated());
				Test->TestFalse(TEXT("The player is back on the stage"), Mode->IsOutsideStage(Player->GetActorLocation()));
				Test->TestFalse(TEXT("The CPU is back on the stage"), Mode->IsOutsideStage(CPU->GetActorLocation()));
				UE_LOG(LogTemp, Display, TEXT("OUTOFSTAGE back at %s and %s"), *Player->GetActorLocation().ToString(),
					*CPU->GetActorLocation().ToString());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OutOfStage"), TEXT("02-Back.png")),
					true, false);
				++Stage;
				NextAt = Now + 0.8;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactOutOfStageTest, "ChaosImpact.Versus.OutOfStage",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactOutOfStageTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOutOfStageCommand(this));
	return true;
}

#endif

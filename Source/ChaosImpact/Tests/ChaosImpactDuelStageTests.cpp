#if WITH_DEV_AUTOMATION_TESTS
// The 1v1 stages (2026-11-16): a local duel's rules open the stage select with the four 1v1 stages and ランダム, the card
// picked is the stage the duel is played on, and two CPUs can play a round on it (they move, nobody is stuck in a wall).
// Run on a VS level (?CITraining=1?CITargets=0?CICPUCount=0). -CIDuelStagePick=N picks card N (default 2, 夕焼けコート).
// Saved/DuelStageQA/*.png.
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactDuelStage.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindDuelStageController()
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

	void DuelStageShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DuelStageQA"), Name), true, false);
	}

	class FDuelStageCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDuelStageCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIDuelStagePick="), Pick);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 200.0)
			{
				Test->AddError(FString::Printf(TEXT("Stalled at step %d"), Step));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindDuelStageController();
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			switch (Step)
			{
			case 0:
				if (!PC || !Menu || Now - StartedAt < 3.0)
				{
					return false;
				}
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusLocal();
				PC->PrepareTrainingControllerAssignment(1);
				PC->RegisterKeyboardMouseJoin();
				PC->ConfirmControllerAssignments();
				PC->ConfirmCharacterSelection();
				PC->SetMatchMode(5);
				Test->TestTrue(TEXT("The rules are a duel"), PC->GetPendingMatchRules().bDuel);
				PC->OpenStageSelect();
				Test->TestTrue(TEXT("A duel's rules open the stage select"), PC->GetCurrentScreen() == EChaosImpactScreen::StageSelect);
				Test->TestEqual(TEXT("Four 1v1 stages, ランダム and the back button"), Menu->GetEntryCount(), ChaosImpactMatch::DuelStageCount + 2);
				Step = 1;
				NextAt = Now + 1.4;
				return false;
			case 1:
				DuelStageShot(TEXT("01-DuelStageSelect.png"));
				Step = 2;
				NextAt = Now + 0.4;
				return false;
			case 2:
				PC->ChooseStage(Pick);
				Test->TestEqual(TEXT("The card picked is the duel's stage"), PC->GetPendingMatchRules().StageIndex, Pick);
				Step = 3;
				NextAt = Now + 2.0;
				return false;
			case 3:
			{
				const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
				if (!Match || !Match->bVersusMatch || !Match->IsDuel())
				{
					return false;
				}
				int32 Found = 0;
				int32 Others = 0;
				for (TActorIterator<AChaosImpactDuelStage> It(World); It; ++It)
				{
					(It->GetDuelStageIndex() == Pick ? Found : Others) += 1;
				}
				if (Found == 0)
				{
					return false;
				}
				Test->TestEqual(TEXT("The duel is on the stage picked"), Match->Rules.StageIndex, Pick);
				Test->TestEqual(TEXT("One 1v1 stage there"), Found, 1);
				Test->TestEqual(TEXT("No other 1v1 stage left there"), Others, 0);
				// The player's side played by a CPU too, so two CPUs fight on it.
				if (APawn* Mine = PC->GetPawn())
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
				Step = 4;
				NextAt = Now + 6.0;
				return false;
			}
			case 4:
				DuelStageShot(TEXT("02-DuelOnStage.png"));
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					Start.Add(*It, It->GetActorLocation());
				}
				Step = 5;
				NextAt = Now + 25.0;
				return false;
			case 5:
			{
				// Each CPU has gone somewhere (not stuck where it stood), and stays on the stage.
				const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
				const FVector Centre = Match ? Match->StageCenter : FVector::ZeroVector;
				for (const TPair<TWeakObjectPtr<AChaosImpactCharacter>, FVector>& Was : Start)
				{
					const AChaosImpactCharacter* Who = Was.Key.Get();
					if (!Who || Who->IsEliminated())
					{
						continue;
					}
					const FVector Now3 = Who->GetActorLocation();
					UE_LOG(LogTemp, Display, TEXT("DUELSTAGE %s moved %.0f, at %s"), *Who->GetName(), FVector::Dist2D(Now3, Was.Value), *(Now3 - Centre).ToCompactString());
					Test->TestTrue(TEXT("Inside the stage"), FMath::Abs(Now3.X - Centre.X) < AChaosImpactDuelStage::DuelHalfExtent + 50.0f
						&& FMath::Abs(Now3.Y - Centre.Y) < AChaosImpactDuelStage::DuelHalfExtent + 50.0f && Now3.Z > Centre.Z - 200.0f);
				}
				DuelStageShot(TEXT("03-DuelLater.png"));
				Step = 6;
				NextAt = Now + 1.0;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		int32 Pick = 2;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> Start;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDuelStageTest, "ChaosImpact.Versus.DuelStages",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactDuelStageTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDuelStageCommand(this));
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
// A 1v1 stage's opening and its map: the opening's camera sweep (taken at a few moments, the camera inside the stage's
// sky the whole time) and the 全体マップ once the round is on. Run on the duel's level with the stage:
// /Game/ChaosImpact/Versus/Levels/Lvl_VS_Duel?CITraining=1?CITargets=0?CIVersus=1?CIMatch=1?CIMatchCPU=0?CIDuel=1?CIStage=N.
// Saved/DuelOpeningQA/Stage<N+1>_*.png.
#include "ChaosImpactDuelStage.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FDuelOpeningCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDuelOpeningCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			const AChaosImpactGameState* Match = PC ? PC->GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->IsDuel())
			{
				return false;
			}
			const int32 StageNumber = Match->Rules.StageIndex + 1;
			const auto Shot = [StageNumber](const TCHAR* What)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DuelOpeningQA"),
					FString::Printf(TEXT("Stage%d_%s.png"), StageNumber, What)), true, false);
			};
			// The camera stays inside the sky the whole opening (the sky's middle is the stage's).
			if (Match->Phase == EChaosImpactOnlinePhase::Intro && PC->PlayerCameraManager)
			{
				const float Reach = FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Match->StageCenter + FVector(0.0f, 0.0f, -300.0f));
				Farthest = FMath::Max(Farthest, Reach);
			}
			const float Intro = Match->Phase == EChaosImpactOnlinePhase::Intro ? Match->GetIntroElapsedSeconds() : -1.0f;
			if (!bRestarted)
			{
				// (The level may have played its opening before this test began: the match is set up again, opening and all.)
				if (Now - StartedAt < 2.0)
				{
					return false;
				}
				if (AChaosImpactGameMode* Mode = PC->GetWorld()->GetAuthGameMode<AChaosImpactGameMode>())
				{
					Mode->ConfigureVersusMatch(Match->Rules);
				}
				bRestarted = true;
				Farthest = 0.0f;
				return false;
			}
			switch (Step)
			{
			case 0:
				if (Intro >= 0.2f) { Shot(TEXT("1_OpeningStart")); ++Step; }
				return false;
			case 1:
				if (Intro >= 1.4f) { Shot(TEXT("2_OpeningMiddle")); ++Step; }
				return false;
			case 2:
				if (Intro >= 2.6f) { Shot(TEXT("3_OpeningLate")); ++Step; }
				return false;
			case 3:
				if (Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				Test->TestTrue(FString::Printf(TEXT("The opening's camera stayed inside the sky (farthest %.0f)"), Farthest), Farthest < 4200.0f);
				NextAt = Now + 5.0;
				++Step;
				return false;
			case 4:
				if (Now < NextAt) { return false; }
				PC->SetOverviewMapOpen(true);
				NextAt = Now + 1.0;
				++Step;
				return false;
			case 5:
				if (Now < NextAt) { return false; }
				Shot(TEXT("4_Map"));
				NextAt = Now + 0.6;
				++Step;
				return false;
			default:
				if (Now < NextAt) { return false; }
				PC->SetOverviewMapOpen(false);
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		float Farthest = 0.0f;
		bool bRestarted = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDuelOpeningTest, "ChaosImpact.Versus.DuelOpening",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactDuelOpeningTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDuelOpeningCommand(this));
	return true;
}
#endif

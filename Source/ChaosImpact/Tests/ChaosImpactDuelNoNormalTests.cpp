#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * A duel with the normal ball switched off (listen host, -CIAutoVersus=0:1:3:duel -CIDevOffBalls=1): once the slot
	 * draw is done, no pad has a normal ball on it (the ones put out before the draw are swapped), and a moment into the
	 * fight none has come out since.
	 */
	class FDuelNoNormalCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDuelNoNormalCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d."), Stage));
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->IsDuel() || Now < NextAt)
			{
				return false;
			}
			const EChaosImpactDuelStep Step = Match->Duel.Step;
			if (Stage == 0)
			{
				if (Step == EChaosImpactDuelStep::BallDraw || Step == EChaosImpactDuelStep::None)
				{
					return false;
				}
				Test->TestFalse(TEXT("Normal switched off in the rules"), ChaosImpactBallSwitch::IsOn(Match->Rules, EChaosImpactBallType::Normal));
				Test->TestEqual(TEXT("Three slot balls drawn"), Match->Duel.BallPicks.Num(), 3);
				Check(World, TEXT("after the draw"));
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SwitchQA"), TEXT("duel_no_normal.png")), true, false);
				Stage = 1;
				NextAt = Now + 8.0;
				return false;
			}
			Check(World, TEXT("into the fight"));
			return true;
		}

	private:
		void Check(UWorld* World, const TCHAR* When)
		{
			int32 Balls = 0;
			int32 Normals = 0;
			for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
			{
				if (const AChaosImpactBall* Ball = It->GetActiveBall())
				{
					++Balls;
					Normals += Ball->GetBallType() == EChaosImpactBallType::Normal ? 1 : 0;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("DUEL NO NORMAL %s: %d balls on pads, %d normal"), When, Balls, Normals);
			Test->TestTrue(FString::Printf(TEXT("Balls on the pads %s"), When), Balls > 0);
			Test->TestEqual(FString::Printf(TEXT("No normal ball on a pad %s"), When), Normals, 0);
		}

		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDuelNoNormalTest, "ChaosImpact.BallSwitch.DuelNoNormal",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDuelNoNormalTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDuelNoNormalCommand(this));
	return true;
}

#endif

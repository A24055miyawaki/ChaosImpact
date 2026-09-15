#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** Run in a VS match level longer than a minute (e.g. -CIMatchSeconds=64): captures the "あと1分" call. */
	class FMinuteLeftCommand : public IAutomationLatentCommand
	{
	public:
		explicit FMinuteLeftCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			const AChaosImpactGameState* Match = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					Match = Context.World()->GetGameState<AChaosImpactGameState>();
					break;
				}
			}
			const bool bPlaying = Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match;
			if (!bPlaying || Match->GetPhaseRemainingSeconds() > 59.4f)
			{
				if (Now - StartedAt < 200.0)
				{
					return false;
				}
				Test->AddError(TEXT("The match never reached one minute left."));
				return true;
			}
			if (Shots >= 2)
			{
				// Wait for the last capture to be written before finishing.
				return Now >= NextShotAt;
			}
			if (Shots == 0 || Now >= NextShotAt)
			{
				UE_LOG(LogTemp, Display, TEXT("MINUTETEST capture %d at %.2f s left"), Shots, Match->GetPhaseRemainingSeconds());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VersusQA"),
					FString::Printf(TEXT("MinuteLeft-%d.png"), Shots)), true, false);
				++Shots;
				NextShotAt = Now + 0.9;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextShotAt = 0.0;
		int32 Shots = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMinuteLeftTest, "ChaosImpact.Versus.MinuteLeftCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactMinuteLeftTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMinuteLeftCommand(this));
	return true;
}

#endif

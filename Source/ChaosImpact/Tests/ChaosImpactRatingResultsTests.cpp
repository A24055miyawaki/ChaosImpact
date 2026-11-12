#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The rating card on the results (run with -CIDevRatedResults=<kind> on a short local VS match, as ChaosImpact.Versus.Results):
	 * filmed through its show (the change drawn into the number, the digits turning and stopping; the measuring's end,
	 * the digits stopping on the rating), then on the numbers page. Saved/RatingQA/<kind>_*.png.
	 */
	class FRatingResultsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRatingResultsCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIDevRatedResults="), Kind);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at shot %d"), Shot));
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
			UChaosImpactResultsView* Results = PC ? PC->GetResultsView() : nullptr;
			if (!Results || !Results->IsActive())
			{
				return false;
			}
			const double AnimStart = Results->GetRatingAnimStart();
			if (Kind.IsEmpty())
			{
				Test->AddError(TEXT("Run with -CIDevRatedResults=<kind>"));
				return true;
			}
			// (pending / void: no show of its own; filmed by the results' clock.)
			const bool bTimed = Kind != TEXT("pending") && Kind != TEXT("void");
			if (bTimed && AnimStart <= 0.0)
			{
				return false;
			}
			const float Since = bTimed ? static_cast<float>(Now - AnimStart) : Results->GetShowSeconds() - 5.0f;
			static const float Times[] = {0.35f, 0.75f, 1.2f, 1.6f, 2.0f, 3.0f};
			if (Shot < UE_ARRAY_COUNT(Times))
			{
				if (Since >= Times[Shot])
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RatingQA"),
						FString::Printf(TEXT("%s_%d.png"), *Kind, Shot + 1)), true, false);
					++Shot;
				}
				return false;
			}
			if (Results->GetPage() == 0)
			{
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Gamepad_FaceButton_Right, FModifierKeysState(), 0, false, 0, 0));
				PressedAt = Now;
				return false;
			}
			if (Now - PressedAt < 1.0)
			{
				return false;
			}
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RatingQA"),
				FString::Printf(TEXT("%s_stats.png"), *Kind)), true, false);
			Test->TestTrue(TEXT("On the numbers page"), Results->GetPage() == 1);
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		FString Kind;
		double StartedAt = FPlatformTime::Seconds();
		double PressedAt = 0.0;
		int32 Shot = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactRatingResultsTest, "ChaosImpact.Versus.RatingResults",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactRatingResultsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRatingResultsCommand(this));
	return true;
}

#endif

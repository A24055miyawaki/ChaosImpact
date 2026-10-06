#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Local VS from the title: the VS card plays over the menu, the VS level loads, opens dark behind the VS emblem and
	 * opens from the middle as the match's opening starts. Screenshots of each moment go to Saved/VersusCardQA.
	 * Run on the title (the level with no options), 1600x900.
	 */
	class FVersusCardCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVersusCardCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				return true;
			}
			if (Stage >= 1 && Stage <= 3)
			{
				// Frames while the card plays (a stall here eats its animation).
				SlowestCardFrame = FMath::Max(SlowestCardFrame, Now - LastFrameAt);
				if (Now - LastFrameAt > 0.1)
				{
					UE_LOG(LogTemp, Display, TEXT("VSCARD slow frame %.0f ms, from %.2f s to %.2f s of the card"),
						(Now - LastFrameAt) * 1000.0, LastFrameAt - CardAt, Now - CardAt);
				}
				if (FirstFrameAfterCard < 0.0)
				{
					FirstFrameAfterCard = Now - CardAt;
				}
			}
			LastFrameAt = Now;
			if (!World || Now < NextAt)
			{
				return false;
			}
			const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
			switch (Stage)
			{
			case 0:
			{
				if (PC->GetCurrentScreen() != EChaosImpactScreen::Title || World->URL.HasOption(TEXT("CITraining=1")))
				{
					return false;
				}
				PC->BeginVersusLocal();
				FChaosImpactMatchRules& Rules = const_cast<FChaosImpactMatchRules&>(PC->GetPendingMatchRules());
				Rules.CPUCount = 3;
				Rules.TeamCount = 0;
				Rules.Minutes = 3;
				Rules.CPULevel = 3;
				PC->ConfirmMatchRules();
				CardAt = Now;
				Stage = 1;
				NextAt = Now + 0.15;
				return false;
			}
			case 1:
				Test->TestTrue(TEXT("The card plays before the VS level loads"), !World->URL.HasOption(TEXT("CIMatch=1")));
				Stage = 2;
				NextAt = CardAt + 0.7;
				return false;
			case 2:
				Shoot(TEXT("2_VersusLands"));
				Stage = 3;
				NextAt = CardAt + 1.35;
				return false;
			case 3:
				UE_LOG(LogTemp, Display, TEXT("VSCARD first frame %.0f ms after the card started, slowest frame while it played %.0f ms"),
					FirstFrameAfterCard * 1000.0, SlowestCardFrame * 1000.0);
				Shoot(TEXT("3_CardSettled"));
				Stage = 4;
				return false;
			case 4:
				if (!World->URL.HasOption(TEXT("CIMatch=1")))
				{
					return false;
				}
				LoadedAt = Now;
				UE_LOG(LogTemp, Display, TEXT("VSCARD VS level opened %.2f s after the card started"), Now - CardAt);
				Shoot(TEXT("4_LevelOpensDark"));
				Stage = 5;
				NextAt = Now + 0.1;
				return false;
			case 5:
				if (!Match || Match->Phase != EChaosImpactOnlinePhase::Intro)
				{
					if (Now - LoadedAt > 20.0)
					{
						Test->AddError(TEXT("The match's opening never started."));
						return true;
					}
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("VSCARD opening started %.2f s after the level opened"), Now - LoadedAt);
				IntroAt = Now;
				Stage = 6;
				NextAt = Now + 0.3;
				return false;
			case 6:
				Shoot(TEXT("5_Opening"));
				Stage = 7;
				NextAt = IntroAt + 0.6;
				return false;
			case 7:
				Shoot(TEXT("6_Opening"));
				Stage = 8;
				NextAt = IntroAt + 1.6;
				return false;
			case 8:
				Shoot(TEXT("7_Opened"));
				Test->TestTrue(TEXT("The match's opening plays"), Match && Match->Phase == EChaosImpactOnlinePhase::Intro);
				Stage = 9;
				NextAt = Now + 1.0;
				return false;
			default:
				return true;
			}
		}

	private:
		static void Shoot(const TCHAR* Name)
		{
			FScreenshotRequest::RequestScreenshot(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VersusCardQA"), FString(Name) + TEXT(".png")), true, false);
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double CardAt = 0.0;
		double LoadedAt = 0.0;
		double IntroAt = 0.0;
		double LastFrameAt = 0.0;
		double SlowestCardFrame = 0.0;
		double FirstFrameAfterCard = -1.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactVersusCardTest, "ChaosImpact.Menu.VersusCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactVersusCardTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVersusCardCommand(this));
	return true;
}

#endif

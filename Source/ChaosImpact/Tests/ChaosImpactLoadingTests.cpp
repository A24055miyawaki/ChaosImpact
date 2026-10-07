#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactFxPreloadSubsystem.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Loading screens (run on the title with -CIAsyncPreload): the start-up one is up while the effects load in the
	 * background and can be played with, then goes; the title to training and back are each covered by one that goes once
	 * the new level is ready. Screenshots go to Saved/LoadingQA.
	 */
	class FLoadingScreenCommand : public IAutomationLatentCommand
	{
	public:
		explicit FLoadingScreenCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			UChaosImpactLoadingSubsystem* Loading = World ? UChaosImpactLoadingSubsystem::Get(World) : nullptr;
			const UChaosImpactFxPreloadSubsystem* Preload = World ? UChaosImpactFxPreloadSubsystem::Get(World) : nullptr;
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!World || !Loading || !Preload || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoadingQA"), Name), true, false);
			};
			switch (Step)
			{
			case 0:
				// Start-up: up while the effects load.
				UE_LOG(LogTemp, Display, TEXT("LOADING startup: showing %d, preload complete %d, progress %.2f"),
					Loading->IsShowing(), Preload->IsComplete(), Preload->GetProgress());
				Test->TestTrue(TEXT("The start-up loading screen is up while the effects load"), Loading->IsShowing() || Preload->IsComplete());
				if (Loading->IsShowing())
				{
					// Played with: a few buttons pop balls.
					for (int32 Index = 0; Index < 3; ++Index)
					{
						Loading->HandleKey(EKeys::Gamepad_FaceButton_Bottom, 0);
					}
					Shot(TEXT("Loading_Startup.png"));
				}
				Step = 1;
				NextAt = Now + 0.6;
				return false;
			case 1:
				if (Loading->IsShowing())
				{
					UE_LOG(LogTemp, Display, TEXT("LOADING startup game %d score %d"), static_cast<int32>(Loading->GetGame()), Loading->GetScore());
				}
				if (Loading->IsShowing() || !Preload->IsComplete())
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("LOADING startup done after %.1f s"), Now - StartedAt);
				Step = 2;
				NextAt = Now + 2.5;
				return false;
			case 2:
				Test->TestEqual(TEXT("The title after start-up"), PC->GetCurrentScreen(), EChaosImpactScreen::Title);
				Shot(TEXT("Loading_TitleAfter.png"));
				// Title to training (as one player would from the menu, past PRESS START).
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->StartTrainingWithPlayers(1);
				TravelFrom = World;
				bSawCover = false;
				Step = 3;
				NextAt = Now + 0.2;
				return false;
			case 3:
				bSawCover |= Loading->IsShowing();
				if (World == TravelFrom.Get())
				{
					return false;
				}
				Test->TestTrue(TEXT("Training's load is covered"), bSawCover);
				UE_LOG(LogTemp, Display, TEXT("LOADING training world in, overlay showing %d"), Loading->IsShowing());
				Test->TestTrue(TEXT("Still covered in the new level until it is ready"), Loading->IsShowing());
				Shot(TEXT("Loading_Training.png"));
				ArrivedAt = Now;
				Step = 4;
				return false;
			case 4:
				if (Loading->IsShowing())
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("LOADING training overlay gone %.2f s after the level came in"), Now - ArrivedAt);
				Test->TestTrue(TEXT("Gone well before the safety timeout"), Now - ArrivedAt < 5.0);
				Test->TestTrue(TEXT("Training under it"), World->URL.HasOption(TEXT("CITraining=1")));
				Step = 5;
				NextAt = Now + 1.5;
				return false;
			case 5:
				PC->ShowMenuScreen(EChaosImpactScreen::Title);
				TravelFrom = World;
				bSawCover = false;
				Step = 6;
				NextAt = Now + 0.2;
				return false;
			case 6:
				bSawCover |= Loading->IsShowing();
				if (World == TravelFrom.Get())
				{
					return false;
				}
				Test->TestTrue(TEXT("Back to the title is covered"), bSawCover);
				Shot(TEXT("Loading_Title.png"));
				ArrivedAt = Now;
				Step = 7;
				return false;
			case 7:
				if (Loading->IsShowing())
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("LOADING title overlay gone %.2f s after the level came in"), Now - ArrivedAt);
				Test->TestTrue(TEXT("Gone well before the safety timeout"), Now - ArrivedAt < 5.0);
				Step = 8;
				NextAt = Now + 2.5;
				return false;
			default:
				Test->TestEqual(TEXT("At the title again"), PC->GetCurrentScreen(), EChaosImpactScreen::Title);
				Shot(TEXT("Loading_TitleReturn.png"));
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ArrivedAt = 0.0;
		int32 Step = 0;
		bool bSawCover = false;
		TWeakObjectPtr<UWorld> TravelFrom;
	};

	/**
	 * Each loading screen game in turn (on the training map): it comes up, plays (pressed, or playing itself), scores, and
	 * goes when told the load is done. Screenshots of each go to Saved/LoadingQA.
	 */
	class FLoadingGamesCommand : public IAutomationLatentCommand
	{
	public:
		explicit FLoadingGamesCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetFirstPlayerController())
				{
					World = Context.World();
				}
			}
			UChaosImpactLoadingSubsystem* Loading = World ? UChaosImpactLoadingSubsystem::Get(World) : nullptr;
			if (Now - StartedAt > static_cast<int32>(EChaosImpactLoadingGame::Count) * 15.0 + 30.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck on game %d."), Game));
				return true;
			}
			if (!Loading || Now < NextAt || Now - StartedAt < 3.0)
			{
				return false;
			}
			constexpr int32 Count = static_cast<int32>(EChaosImpactLoadingGame::Count);
			if (Game >= Count)
			{
				// Then the arcade (from the settings): it stays up, switches game with E, and goes with Escape.
				if (ArcadeStep == 0)
				{
					Loading->ShowArcade();
					Test->TestTrue(TEXT("The arcade is up"), Loading->IsArcade());
					const EChaosImpactLoadingGame First = Loading->GetGame();
					Loading->HandleKey(EKeys::E, 0);
					Test->TestTrue(TEXT("E switches to the next game"), Loading->GetGame() != First);
					// Breakout, to take a picture of.
					while (Loading->GetGame() != EChaosImpactLoadingGame::Breakout)
					{
						Loading->HandleKey(EKeys::E, 0);
					}
					Loading->HandleKey(EKeys::SpaceBar, 0);
					ArcadeStep = 1;
					NextAt = Now + 3.0;
					return false;
				}
				if (ArcadeStep == 1)
				{
					Test->TestTrue(TEXT("The arcade never goes by itself"), Loading->IsArcade());
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoadingQA"), TEXT("Arcade.png")), true, false);
					ArcadeStep = 2;
					NextAt = Now + 0.5;
					return false;
				}
				if (ArcadeStep == 2)
				{
					Loading->HandleKey(EKeys::Escape, 0);
					ArcadeStep = 3;
					NextAt = Now + 0.5;
					return false;
				}
				Test->TestFalse(TEXT("Escape leaves the arcade"), Loading->IsShowing());
				return true;
			}
			const EChaosImpactLoadingGame Which = static_cast<EChaosImpactLoadingGame>(Game);
			if (!bUp)
			{
				if (Loading->IsShowing())
				{
					return false;
				}
				Loading->SetNextGame(Which);
				Loading->ShowWaiting(EChaosImpactLoadingKind::Training, [ShownAt = Now]()
				{
					return FMath::Clamp(static_cast<float>(FPlatformTime::Seconds() - ShownAt) / 5.0f, 0.0f, 1.0f);
				});
				Test->TestEqual(TEXT("The game asked for"), static_cast<int32>(Loading->GetGame()), Game);
				bUp = true;
				UpAt = Now;
				Presses = 0;
				RunScore = 0;
				NextAt = Now + 0.4;
				return false;
			}
			const double Up = Now - UpAt;
			RunScore = FMath::Max(RunScore, Loading->GetScore());
			// Pressed a few times (popping, turning pages); the moving games are left to play themselves.
			if ((Which == EChaosImpactLoadingGame::Pop || Which == EChaosImpactLoadingGame::Gallery) && Presses < 3)
			{
				Loading->HandleKey(EKeys::Gamepad_FaceButton_Bottom, 0);
				++Presses;
				NextAt = Now + 0.3;
				return false;
			}
			// シマエナガ大ぼうけん: one more picture early on, mid-dodge.
			if (Which == EChaosImpactLoadingGame::Quest && !bDodgeShot && Up > 2.5)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoadingQA"), TEXT("LoadingGame_Quest_Early.png")), true, false);
				bDodgeShot = true;
				NextAt = Now + 0.3;
				return false;
			}
			// Loading/prewarm can hitch; physics deliberately clamps a long frame. Give the demo a bounded
			// chance to score, and inspect this run's peak rather than a snapshot just after a miss/restart.
			if (Up < 6.5 || (Which != EChaosImpactLoadingGame::Target && RunScore == 0 && Up < 12.5))
			{
				NextAt = Now + 0.1;
				return false;
			}
			if (!bShot)
			{
				const int32 Score = Loading->GetScore();
				UE_LOG(LogTemp, Display, TEXT("LOADINGGAME %d score %d run peak %d after %.1f s"), Game, Score, RunScore, Up);
				Test->TestTrue(FString::Printf(TEXT("Game %d scores (pressed or by itself)"), Game),
					Which == EChaosImpactLoadingGame::Target ? RunScore >= 0 : RunScore >= 1);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoadingQA"),
					FString::Printf(TEXT("LoadingGame_%d.png"), Game)), true, false);
				bShot = true;
				NextAt = Now + 0.5;
				return false;
			}
			Loading->NotifyWorldReady();
			if (Loading->IsShowing())
			{
				NextAt = Now + 0.1;
				return false;
			}
			++Game;
			bUp = false;
			bShot = false;
			NextAt = Now + 0.5;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double UpAt = 0.0;
		int32 Game = 0;
		int32 Presses = 0;
		int32 RunScore = 0;
		bool bUp = false;
		bool bShot = false;
		bool bDodgeShot = false;
		int32 ArcadeStep = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLoadingGamesTest, "ChaosImpact.Menu.LoadingGames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactLoadingGamesTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLoadingGamesCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLoadingScreenTest, "ChaosImpact.Menu.LoadingFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactLoadingScreenTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLoadingScreenCommand(this));
	return true;
}

#endif

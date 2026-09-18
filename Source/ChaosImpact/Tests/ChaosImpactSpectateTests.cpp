#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSpectatorPawn.h"
#include "ChaosImpactVersusStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindSpectateTestController()
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
	 * A local VS level opened with ?CIMatch=1?CISpectate=1: the player watches from a spectator camera while only
	 * CPUs play, steps through the players' cameras, hides the HUD, flies inside the stage, and after the results
	 * plays the rematch themselves. Use -CIMatchSeconds= to keep the match short.
	 */
	class FSpectateCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSpectateCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSpectateTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			AChaosImpactSpectatorPawn* Camera = PC ? Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()) : nullptr;
			const AChaosImpactPlayerState* Own = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			const auto Capture = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpectateQA"), Name),
					true, false);
			};
			const auto Fail = [this](const FString& Message)
			{
				Test->AddError(Message);
				return true;
			};
			if (Now - StartedAt > 240.0)
			{
				return Fail(FString::Printf(TEXT("Timed out at stage %d."), Stage));
			}
			if (Stage > 0 && (!Match || !Own || ((Stage <= 6 || (Stage >= 20 && Stage < 80)) && !Camera)))
			{
				return Fail(FString::Printf(TEXT("Lost the spectator camera or match at stage %d."), Stage));
			}

			switch (Stage)
			{
			case 0:
				// Watching from the start: no character, not a competitor, only CPUs in the match.
				if (!Match || !Match->bVersusMatch || !Camera || !Own)
				{
					return false;
				}
				if (!Own->bSpectating)
				{
					return Fail(TEXT("The player should be marked as spectating."));
				}
				for (const AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
				{
					if (!Member->IsABot())
					{
						return Fail(TEXT("Only CPUs should compete while the player watches."));
					}
				}
				Test->TestEqual(TEXT("CPUs in the watched match"), Match->GetCompetitors(true).Num(), Match->Rules.CPUCount);
				Test->TestTrue(TEXT("At least two CPUs play"), Match->Rules.CPUCount >= 2);
				Test->TestTrue(TEXT("The match rules say spectate"), Match->Rules.bSpectate);
				{
					FChaosImpactMatchRules Watching = PC->GetPendingMatchRules();
					Watching.bSpectate = true;
					Watching.CPUCount = 0;
					Test->TestTrue(TEXT("Watching keeps two CPUs"),
						ChaosImpactMatch::Sanitize(Watching, 2).CPUCount >= 2);
					Watching.CPUCount = 8;
					Test->TestEqual(TEXT("Watching allows eight CPUs"), ChaosImpactMatch::Sanitize(Watching, 2).CPUCount, 8);
				}
				Stage = 1;
				return false;
			case 1:
				if (Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				Test->TestTrue(TEXT("Free camera is the view"), PC->GetViewTarget() == Camera);
				for (const AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
				{
					if (const APawn* CPU = Member->GetPawn())
					{
						CPUStarts.Add(CPU, CPU->GetActorLocation());
					}
				}
				CPUStartTime = World->GetTimeSeconds();
				Capture(TEXT("01_FreeCamera"));
				NextAt = Now + 0.6;
				Stage = 2;
				return false;
			case 2:
				// Flying far past the edge is held inside the stage on the next frame.
				Camera->SetActorLocation(Match->StageCenter + FVector(90000.0f, -90000.0f, 90000.0f));
				NextAt = Now + 0.3;
				Stage = 20;
				return false;
			case 20:
			{
				const FVector Held = Camera->GetActorLocation() - Match->StageCenter;
				const float Reach = AChaosImpactVersusStage::HalfExtent + AChaosImpactSpectatorPawn::BoundsMargin + 1.0f;
				Test->TestTrue(FString::Printf(TEXT("Camera stays inside the stage (%s)"), *Held.ToString()),
					FMath::Abs(Held.X) <= Reach && FMath::Abs(Held.Y) <= Reach && Held.Z <= AChaosImpactSpectatorPawn::MaxHeight + 1.0f);
				Camera->CycleFollow(1);
				FirstWatched = Camera->GetFollowedCharacter();
				if (!FirstWatched.IsValid())
				{
					return Fail(TEXT("Next player should show a CPU's camera."));
				}
				NextAt = Now + 1.0;
				Stage = 3;
				return false;
			}
			case 3:
				Test->TestTrue(TEXT("View is the followed player"), PC->GetViewTarget() == FirstWatched.Get());
				Capture(TEXT("02_FollowFirst"));
				NextAt = Now + 0.5;
				Stage = 30;
				return false;
			case 30:
				Camera->CycleFollow(1);
				Test->TestTrue(TEXT("Next player is another CPU"), Camera->GetFollowedCharacter() != FirstWatched.Get());
				NextAt = Now + 1.0;
				Stage = 4;
				return false;
			case 4:
				Capture(TEXT("03_FollowSecond"));
				NextAt = Now + 0.5;
				Stage = 40;
				return false;
			case 40:
				Camera->SetHudHidden(true);
				NextAt = Now + 0.5;
				Stage = 5;
				return false;
			case 5:
				Capture(TEXT("04_HudHidden"));
				NextAt = Now + 0.5;
				Stage = 50;
				return false;
			case 50:
				Camera->SetHudHidden(false);
				Camera->StopFollowing();
				NextAt = Now + 0.6;
				Stage = 51;
				return false;
			case 51:
				Test->TestTrue(TEXT("Free camera again"), PC->GetViewTarget() == Camera && !Camera->GetFollowedCharacter());
				// Time stop: the whole match freezes.
				Camera->SetTimeStopped(true);
				StoppedAt = World->GetTimeSeconds();
				FrozenCPU = Match->GetCompetitors(true).IsEmpty() ? nullptr : Match->GetCompetitors(true)[0]->GetPawn();
				FrozenAt = FrozenCPU.IsValid() ? FrozenCPU->GetActorLocation() : FVector::ZeroVector;
				NextAt = Now + 1.5;
				Stage = 52;
				return false;
			case 52:
				Test->TestTrue(TEXT("Time stop pauses the world"), World->IsPaused() && Camera->IsTimeStopped());
				Test->TestTrue(TEXT("Match time does not pass"), FMath::IsNearlyEqual(World->GetTimeSeconds(), StoppedAt, 0.001));
				Test->TestTrue(TEXT("CPUs do not move"), FrozenCPU.IsValid()
					&& FVector::Dist(FrozenCPU->GetActorLocation(), FrozenAt) < 0.1);
				Capture(TEXT("04b_TimeStopped"));
				NextAt = Now + 0.5;
				Stage = 53;
				return false;
			case 53:
				// The camera still flies: pushed past the edge, it is pulled back in on its own tick.
				Camera->SetActorLocation(Match->StageCenter + FVector(90000.0f, 0.0f, 500.0f));
				NextAt = Now + 0.5;
				Stage = 54;
				return false;
			case 54:
				Test->TestTrue(TEXT("The camera keeps ticking while time is stopped"),
					FMath::Abs(Camera->GetActorLocation().X - Match->StageCenter.X)
						<= AChaosImpactVersusStage::HalfExtent + AChaosImpactSpectatorPawn::BoundsMargin + 1.0f);
				Camera->CycleFollow(1);
				NextAt = Now + 0.5;
				Stage = 55;
				return false;
			case 55:
				Test->TestTrue(TEXT("Player views switch while stopped"), PC->GetViewTarget() == Camera->GetFollowedCharacter());
				Capture(TEXT("04c_TimeStoppedFollow"));
				NextAt = Now + 0.5;
				Stage = 56;
				return false;
			case 56:
				Camera->StopFollowing();
				Camera->SetTimeStopped(false);
				NextAt = Now + 0.5;
				Stage = 57;
				return false;
			case 57:
				Test->TestFalse(TEXT("Time runs again"), World->IsPaused());
				Stage = 6;
				return false;
			case 6:
			{
				// The CPUs play on without a player: they move about over some seconds of match time.
				if (World->GetTimeSeconds() - CPUStartTime < 12.0 && Match->Phase == EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				int32 Points = 0;
				int32 Moved = 0;
				for (const AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
				{
					Points += Member->Points;
					const APawn* CPU = Member->GetPawn();
					const FVector* From = CPU ? CPUStarts.Find(CPU) : nullptr;
					Moved += From && FVector::Dist2D(*From, CPU->GetActorLocation()) > 150.0 ? 1 : 0;
				}
				Test->TestTrue(FString::Printf(TEXT("CPUs moved while watched (%d)"), Moved), Moved >= 2);
				Test->AddInfo(FString::Printf(TEXT("CPU points so far: %d"), Points));
				Stage = 7;
				return false;
			}
			case 7:
				if (Match->Phase != EChaosImpactOnlinePhase::Results)
				{
					return false;
				}
				NextAt = Now + 3.0;
				Stage = 8;
				return false;
			case 8:
				Capture(TEXT("05_Results"));
				NextAt = Now + 4.5;
				Stage = 80;
				return false;
			case 80:
				Capture(TEXT("05b_ResultsRevealed"));
				NextAt = Now + 0.5;
				Stage = 81;
				return false;
			case 81:
				// もう一度: watching again.
				PC->RetryVersusMatch();
				NextAt = Now + 1.0;
				Stage = 9;
				return false;
			case 9:
			{
				if (!Camera || Match->Phase == EChaosImpactOnlinePhase::Results)
				{
					return false;
				}
				Test->TestTrue(TEXT("Still watching after もう一度"), Own->bSpectating && Match->GetCompetitors(false).IsEmpty());
				NextAt = Now + 8.0;
				Stage = 10;
				return false;
			}
			case 10:
				Capture(TEXT("06_WatchingAgain"));
				NextAt = Now + 0.5;
				Stage = 11;
				return false;
			case 11:
				// ルールを変える while watching.
				PC->OpenMatchRules(EChaosImpactScreen::Playing);
				NextAt = Now + 1.5;
				Stage = 12;
				return false;
			case 12:
				Capture(TEXT("07_RulesWatch"));
				NextAt = Now + 0.5;
				Stage = 13;
				return false;
			default:
				PC->CancelMatchRules();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TMap<TWeakObjectPtr<const APawn>, FVector> CPUStarts;
		double CPUStartTime = 0.0;
		TWeakObjectPtr<AChaosImpactCharacter> FirstWatched;
		double StoppedAt = 0.0;
		TWeakObjectPtr<APawn> FrozenCPU;
		FVector FrozenAt = FVector::ZeroVector;
	};

	/**
	 * From the menu: VS → ローカル → 観戦 goes straight to the rules (no controller assignment or characters) and
	 * opens a VS level where the lone player watches.
	 */
	class FLocalSpectateEntryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FLocalSpectateEntryCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d."), Stage));
				return true;
			}
			AChaosImpactPlayerController* PC = FindSpectateTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			if (!World)
			{
				return false;
			}
			const auto Capture = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpectateQA"), Name),
					true, false);
			};
			switch (Stage)
			{
			case 0:
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusLocal();
				NextAt = Now + 1.0;
				Stage = 1;
				return false;
			case 1:
				Test->TestTrue(TEXT("VS local opens the player count screen"),
					PC->GetCurrentScreen() == EChaosImpactScreen::TrainingSetup);
				Capture(TEXT("00_PlayerCountWithSpectate"));
				NextAt = Now + 0.6;
				Stage = 2;
				return false;
			case 2:
				PC->BeginLocalSpectate();
				Test->TestTrue(TEXT("観戦 goes straight to the rules"), PC->GetCurrentScreen() == EChaosImpactScreen::MatchRules);
				Test->TestTrue(TEXT("The rules are for watching"), PC->GetPendingMatchRules().bSpectate);
				Test->TestEqual(TEXT("Watching is for one player"), PC->GetMatchHumanCount(), 1);
				NextAt = Now + 1.0;
				Stage = 3;
				return false;
			case 3:
				Capture(TEXT("00b_RulesWatching"));
				NextAt = Now + 0.6;
				Stage = 4;
				return false;
			case 4:
				PC->ConfirmMatchRules();
				Stage = 5;
				return false;
			default:
			{
				// The VS level with the player watching.
				const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
				const AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>();
				if (!Match || !Match->bVersusMatch || !Own || !Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()))
				{
					return false;
				}
				Test->TestTrue(TEXT("Spectating in the new level"), Own->bSpectating && Match->Rules.bSpectate);
				Test->TestEqual(TEXT("One local player"), World->GetGameInstance()->GetLocalPlayers().Num(), 1);
				Test->TestTrue(TEXT("Only CPUs compete"), Match->GetCompetitors(false).IsEmpty()
					&& Match->GetCompetitors(true).Num() >= 2);
				return true;
			}
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLocalSpectateEntryTest, "ChaosImpact.UI.LocalSpectateEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactLocalSpectateEntryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLocalSpectateEntryCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSpectateTest, "ChaosImpact.Versus.Spectate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSpectateTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSpectateCommand(this));
	return true;
}

#endif

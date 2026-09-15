#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindVersusTestController()
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
	 * Drives a local VS level opened with ?CITraining=1?CIMatch=1 (any rules, optional split screen):
	 * team select, the opening camera, GO, scoring, results and the rematch menu.
	 * Use -CIMatchSeconds= to keep the match short.
	 */
	class FVersusMatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVersusMatchCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindVersusTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const auto Waited = [&](const double Limit)
			{
				return Now - StartedAt >= Limit;
			};
			if (!Match || !Player)
			{
				if (!Waited(60.0))
				{
					return false;
				}
				Test->AddError(TEXT("Run with ?CITraining=1?CIMatch=1 so a VS match starts."));
				return true;
			}
			const auto OwnPoints = [PC]()
			{
				const AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>();
				return Own ? Own->Points : -1;
			};
			const auto Capture = [this](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VersusQA"),
					Prefix + Name), true, false);
			};

			switch (Stage)
			{
			case 0:
			{
				if (Match->bVersusMatch && !bRestarted && Match->Phase != EChaosImpactOnlinePhase::TeamSelect
					&& (Match->Phase != EChaosImpactOnlinePhase::Intro || Match->GetPhaseElapsedSeconds() > 0.8f))
				{
					// The automation queue starts long after the level: the match is already under way, so
					// start it over the way the rematch menu does and follow it from the beginning.
					bRestarted = true;
					PC->RetryVersusMatch();
					NextAt = Now + 0.3;
					return false;
				}
				if (!Match->bVersusMatch || (Match->Phase != EChaosImpactOnlinePhase::TeamSelect
					&& Match->Phase != EChaosImpactOnlinePhase::Intro))
				{
					if (!Waited(60.0))
					{
						return false;
					}
					Test->AddError(TEXT("The VS match never reached team select or the opening."));
					return true;
				}
				const int32 LocalPlayers = World->GetGameInstance() ? World->GetGameInstance()->GetLocalPlayers().Num() : 1;
				Prefix = FString::Printf(TEXT("%s%s-"), Match->IsTeamBattle() ? TEXT("Team") : TEXT("Solo"),
					LocalPlayers > 1 ? *FString::Printf(TEXT("%dP"), LocalPlayers) : TEXT(""));
				if (Match->Phase == EChaosImpactOnlinePhase::TeamSelect)
				{
					const AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>();
					TeamBefore = Own ? Own->TeamIndex : INDEX_NONE;
					Test->TestTrue(TEXT("Humans start with a team"), TeamBefore >= 0);
					PC->ChangeOwnTeam(1);
					Stage = 1;
					NextAt = Now + 0.8;
					return false;
				}
				Stage = 3;
				return false;
			}
			case 1:
			{
				const AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>();
				Test->TestTrue(TEXT("The team select screen is open"), PC->GetCurrentScreen() == EChaosImpactScreen::TeamSelect);
				Test->TestTrue(TEXT("A player can move to another team"), Own && Own->TeamIndex != TeamBefore);
				Capture(TEXT("00-TeamSelect.png"));
				Stage = 2;
				NextAt = Now + 0.6;
				return false;
			}
			case 2:
				PC->RequestStartTeamMatch();
				Stage = 3;
				NextAt = Now + 0.3;
				return false;
			case 3:
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Intro)
				{
					if (!Waited(60.0))
					{
						return false;
					}
					Test->AddError(TEXT("The opening did not start."));
					return true;
				}
				IntroClock = Now - Match->GetPhaseElapsedSeconds();
				const TArray<AChaosImpactPlayerState*> Competitors = Match->GetCompetitors();
				Test->TestEqual(TEXT("Every player and CPU competes"), Competitors.Num(),
					Match->CountHumanMembers() + Match->Rules.CPUCount);
				Test->TestTrue(TEXT("Nobody can act during the opening"), Player->IsMatchInputLocked());
				if (Match->IsTeamBattle())
				{
					TArray<int32> Counts;
					Counts.Init(0, Match->Rules.TeamCount);
					bool bAllAssigned = true;
					for (const AChaosImpactPlayerState* Member : Competitors)
					{
						if (Member->TeamIndex < 0 || Member->TeamIndex >= Match->Rules.TeamCount)
						{
							bAllAssigned = false;
						}
						else
						{
							++Counts[Member->TeamIndex];
						}
					}
					Test->TestTrue(TEXT("CPUs are placed in teams too"), bAllAssigned);
					const int32 Capacity = ChaosImpactMatch::GetTeamCapacity(Match->Rules.TeamCount, Competitors.Num());
					for (const int32 Count : Counts)
					{
						Test->TestTrue(TEXT("No team is over capacity"), Count <= Capacity);
					}
				}
				Stage = 4;
				return false;
			}
			// The opening runs on game time, which falls behind the wall clock whenever a frame hitches,
			// so these steps wait on the phase's own elapsed time.
			case 4:
				if (Match->Phase == EChaosImpactOnlinePhase::Intro && Match->GetIntroElapsedSeconds() < 1.3f)
				{
					return false;
				}
				Test->TestTrue(TEXT("The opening camera replaces the player view"), PC->GetViewTarget() != Player);
				Capture(TEXT("01-Flyover.png"));
				Stage = 5;
				return false;
			case 5:
				if (Match->Phase == EChaosImpactOnlinePhase::Intro && Match->GetIntroElapsedSeconds() < 4.95f)
				{
					return false;
				}
				Capture(TEXT("02-Dive.png"));
				Stage = 6;
				return false;
			case 6:
				// Ready? starts once this machine has reported its opening finished.
				if (Match->Phase == EChaosImpactOnlinePhase::Intro
					&& (Match->ReadyStartedAt <= 0.0 || Match->GetServerWorldTimeSeconds() - Match->ReadyStartedAt < 0.5))
				{
					return false;
				}
				Test->TestTrue(TEXT("Ready? still holds everyone"), Player->IsMatchInputLocked());
				Test->TestTrue(TEXT("The camera is back on the player for Ready?"), PC->GetViewTarget() == Player);
				Capture(TEXT("03-Ready.png"));
				Stage = 7;
				return false;
			case 7:
			{
				if (Match->Phase == EChaosImpactOnlinePhase::Intro)
				{
					if (Now - IntroClock < 30.0)
					{
						return false;
					}
					Test->AddError(TEXT("The opening never ended."));
					return true;
				}
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Match->GetPhaseElapsedSeconds() < 0.2f)
				{
					return false;
				}
				Test->TestTrue(TEXT("GO starts the match"), Match->Phase == EChaosImpactOnlinePhase::Match);
				Test->TestFalse(TEXT("Players can act from GO"), Player->IsMatchInputLocked());
				Capture(TEXT("04-GO.png"));
				// Stand the CPUs still so the scoring checks are not dodged.
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					AChaosImpactCharacter* CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
					if (!CPU)
					{
						continue;
					}
					if (AChaosImpactGameState::AreTeammates(World, CPU, Player))
					{
						Teammate = CPU;
					}
					else if (!Opponent.IsValid())
					{
						Opponent = CPU;
					}
				}
				Stage = 8;
				NextAt = Now + 0.5;
				return false;
			}
			case 8:
			{
				AChaosImpactCharacter* Target = Opponent.Get();
				if (!Target)
				{
					Test->AddError(TEXT("No opponent CPU to score on."));
					return true;
				}
				PointsBefore = OwnPoints();
				UGameplayStatics::ApplyDamage(Target, 1.0f, PC, Player, nullptr);
				Test->TestEqual(TEXT("A hit on an opponent scores 1 point"), OwnPoints(), PointsBefore + 1);
				UGameplayStatics::ApplyDamage(Target, 10.0f, PC, Player, nullptr);
				Test->TestTrue(TEXT("The opponent is knocked out"), Target->IsEliminated());
				Test->TestEqual(TEXT("A knockout scores the hit plus the bonus"), OwnPoints(), PointsBefore + 3);
				if (AChaosImpactCharacter* Friend = Teammate.Get())
				{
					const float Health = Friend->GetHealth();
					UGameplayStatics::ApplyDamage(Friend, 1.0f, PC, Player, nullptr);
					Test->TestEqual(TEXT("Teammates cannot hurt each other"), Friend->GetHealth(), Health);
				}
				Stage = 9;
				NextAt = Now + 1.0;
				return false;
			}
			case 9:
				Capture(TEXT("05-Match-HUD.png"));
				Stage = 10;
				return false;
			case 10:
				if (Match->Phase != EChaosImpactOnlinePhase::Results)
				{
					if (!Waited(240.0))
					{
						return false;
					}
					Test->AddError(TEXT("The match never finished (use -CIMatchSeconds= for a short match)."));
					return true;
				}
				ResultsClock = Now - Match->GetPhaseElapsedSeconds();
				Stage = 11;
				NextAt = ResultsClock + 0.6;
				return false;
			case 11:
				Test->TestTrue(TEXT("The results hold everyone in place"), Player->IsMatchInputLocked());
				Capture(TEXT("06-Finish.png"));
				Stage = 12;
				NextAt = ResultsClock + 3.2;
				return false;
			case 12:
				Capture(TEXT("07-Results-Reveal.png"));
				Stage = 15;
				NextAt = ResultsClock + 5.6;
				return false;
			case 15:
				Capture(TEXT("07b-Results-Winner.png"));
				Stage = 13;
				NextAt = ResultsClock + ChaosImpactMatch::ResultsRevealSeconds + 1.0;
				return false;
			case 13:
				Test->TestTrue(TEXT("A local match offers a rematch"), PC->GetCurrentScreen() == EChaosImpactScreen::MatchEnd);
				Capture(TEXT("08-MatchEnd.png"));
				Stage = 14;
				NextAt = Now + 0.5;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double IntroClock = 0.0;
		double ResultsClock = 0.0;
		int32 Stage = 0;
		bool bRestarted = false;
		int32 TeamBefore = INDEX_NONE;
		int32 PointsBefore = 0;
		FString Prefix;
		TWeakObjectPtr<AChaosImpactCharacter> Opponent;
		TWeakObjectPtr<AChaosImpactCharacter> Teammate;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactVersusMatchTest, "ChaosImpact.Versus.LocalMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactVersusMatchTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVersusMatchCommand(this));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactDuelBallBalance.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactSpectatorPawn.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FRoyaleFlow : public IAutomationLatentCommand
	{
	public:
		explicit FRoyaleFlow(FAutomationTestBase* T) : Test(T)
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("CIRoyaleOnlyTie"))) { Case = 4; }
			FParse::Value(FCommandLine::Get(), TEXT("CIRoyaleCase="), Case);
			bSingleCase = FParse::Param(FCommandLine::Get(), TEXT("CIRoyaleSingle"));
		}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 250.) { Test->AddError(FString::Printf(TEXT("Royale flow timeout: case %d step %d"), Case, Step)); return true; }
			UWorld* World = nullptr;
			for (const auto& C : GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { World = C.World(); break; } }
			auto* Mode = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!Mode || !Match || !PC || Now < NextAt) { return false; }
			const int32 Teams = Case == 0 || Case >= 4 ? 0 : Case + 1;
			const auto Shot = [&](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoyaleQA"),
					FString::Printf(TEXT("Team%d_%s.png"), Teams, Name)), true, false);
			};
			if (Step == 0)
			{
				PC->ResumeGameplay();
				FChaosImpactMatchRules Rules; Rules.bBattleRoyale = true; Rules.TeamCount = Teams;
				Rules.CPUCount = Teams > 0 ? Teams * 2 - 1 : 2; Rules.StageIndex = Case % ChaosImpactMatch::StageCount;
				Mode->ConfigureVersusMatch(Rules);
				if (Teams > 0) { Mode->ConfirmTeamsAndStart(); }
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				Step = 1; return false;
			}
			if (Step == 1)
			{
				if (Match->Phase == EChaosImpactOnlinePhase::Intro && Match->GetIntroElapsedSeconds() > .5f && !bIntroShot)
				{ Shot(TEXT("Opening")); bIntroShot = true; }
				if (Match->Phase != EChaosImpactOnlinePhase::Match) { return false; }
				Test->TestTrue(TEXT("Royale rule survives configuration and travel options"), Match->IsBattleRoyale());
				Test->TestEqual(TEXT("All entrants are in the replicated roster"), Match->Royale.Entries.Num(), Teams > 0 ? Teams * 2 : 3);
				for (int32 Team = 0; Team < Teams; ++Team)
				{
					int32 Count = 0; for (const auto& E : Match->Royale.Entries) { Count += E.Team == Team ? 1 : 0; }
					Test->TestEqual(TEXT("Every team starts with the same number"), Count, 2);
				}
				for (int32 I = 0; I < Match->Royale.Entries.Num(); ++I)
				{
					auto& E = Match->Royale.Entries[I];
					auto* Pawn = Cast<AChaosImpactCharacter>(E.Member->GetPawn());
					Pawn->SetTrainingMenuFrozen(true);
					if (Teams == 0 && Case != 5 && I == 0) { Pawn->TakeDamage(2.f, FDamageEvent(), nullptr, nullptr); E.Member->Points = 999; E.Member->Knockouts = 99; }
					if (Case == 5) { Pawn->TakeDamage(static_cast<float>(I), FDamageEvent(), nullptr, nullptr); }
					if (Teams == 2)
					{
						if (E.Team == 0) { Pawn->TakeDamage(2.f, FDamageEvent(), nullptr, nullptr); }
						if (E.Team == 1 && !bReduced) { Pawn->EliminateFromBattleRoyale(); bReduced = true; }
					}
					if (Teams == 3 && E.Team == 2 && !bReduced) { Pawn->TakeDamage(1.f, FDamageEvent(), nullptr, nullptr); bReduced = true; }
					if (Teams == 4 && E.Team == 3 && !bReduced) { Pawn->EliminateFromBattleRoyale(); bReduced = true; }
				}
				PhaseStart = Match->PhaseStartedAt;
				Step = 2; return false;
			}
			if (Step == 2)
			{
				// The HUD in the middle of the match.
				if (!bMidShot && Match->Phase == EChaosImpactOnlinePhase::Match && Match->GetPhaseElapsedSeconds() > 3.f)
				{
					Shot(TEXT("Match")); bMidShot = true;
				}
				// The last seconds before the bell (the clock red-hot).
				if (!bHurryShot && Match->Phase == EChaosImpactOnlinePhase::Match && !Match->Royale.bSuddenDeath
					&& Match->GetPhaseRemainingSeconds() < 7.5f && Match->GetPhaseRemainingSeconds() > 6.f)
				{
					Shot(TEXT("Hurry")); bHurryShot = true;
				}
				if (!Flight.IsValid() && Match->GetPhaseRemainingSeconds() < 2.f && Match->Phase == EChaosImpactOnlinePhase::Match)
				{
					Flight = World->SpawnActor<AChaosImpactBall>(Match->StageCenter + FVector(0,-600,1200), FRotator::ZeroRotator);
					Flight->Launch(FVector::RightVector, 450.f, EChaosImpactBallFlightMode::Straight, 0.f);
				}
				if (!Match->Royale.bSuddenDeath && Match->Phase != EChaosImpactOnlinePhase::Results)
				{
					if (Flight.IsValid()) { LastBallPosition = Flight->GetActorLocation(); }
					return false;
				}
				if (Teams == 2)
				{
					Test->TestFalse(TEXT("More survivors win before comparing team HP"), Match->Royale.bSuddenDeath);
					Test->TestEqual(TEXT("Two players on 1 HP beat one player on 3 HP"), Match->Royale.TeamPlaces[0], 1);
					Step = 4; return false;
				}
				if (Case == 5)
				{
					Test->TestFalse(TEXT("A unique HP leader wins at the bell without sudden death"), Match->Royale.bSuddenDeath);
					for (int32 I = 0; I < 3; ++I) { Test->TestEqual(TEXT("All remaining places use HP order at the bell"), Match->Royale.Entries[I].Place, I + 1); }
					Step = 4; return false;
				}
				Test->TestTrue(TEXT("Only a tie starts sudden death"), Match->Royale.bSuddenDeath);
				Test->TestEqual(TEXT("Sudden death never restarts the phase or intro"), Match->PhaseStartedAt, PhaseStart);
				Test->TestFalse(TEXT("World time remains unpaused"), World->IsPaused());
				Test->TestFalse(TEXT("Actions are not locked by sudden death"), Match->IsMatchInputLocked());
				Test->TestTrue(TEXT("In-flight balls keep moving across the transition"), Flight.IsValid() && !Flight->GetActorLocation().Equals(LastBallPosition, .1));
				for (const auto& E : Match->Royale.Entries)
				{
					if (!E.bOut) { Test->TestEqual(TEXT("All finalists have one HP"), E.Health, 1.f); }
				}
				Test->TestEqual(TEXT("Lower placed players/teams are eliminated immediately"), Match->Royale.AliveSides, Teams == 4 ? 3 : 2);
				Shot(TEXT("SuddenDeath")); NextAt = Now + 2.7; Step = 3; return false;
			}
			if (Step == 3)
			{
				if (!bOutShot) { Shot(TEXT("Eliminated")); bOutShot = true; NextAt = Now + .6; return false; }
				if (Case == 4) { Step = 4; return false; }
				if (Teams == 0)
				{
					Test->TestNotNull(TEXT("Eliminated human switches to their own spectator camera"), Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()));
					Test->TestTrue(TEXT("Eliminated human stays eliminated without respawn"), Match->Royale.Entries[0].bOut);
				}
				for (int32 I = 0; I < Match->Royale.Entries.Num(); ++I)
				{
					auto& E = Match->Royale.Entries[I];
					if (E.bOut) { continue; }
					const bool Kill = Teams == 0 ? I == 1 : E.Team != (Teams == 4 ? 2 : 1);
					if (Kill) { Cast<AChaosImpactCharacter>(E.Member->GetPawn())->EliminateFromBattleRoyale(); }
				}
				Step = 4; return false;
			}
			if (Step == 4)
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Results || Match->GetPhaseElapsedSeconds() < 7.8f
					|| !PC->GetResultsView() || !PC->GetResultsView()->IsActive() || PC->GetResultsView()->GetShowSeconds() < 5.6f) { return false; }
				const auto Data = FChaosImpactResultsData::Capture(Match, PC);
				Test->TestTrue(TEXT("Royale results use survival placements"), Data.bBattleRoyale);
				Test->TestEqual(TEXT("Results draw only when the final leaders are tied"), Data.bTie, Case == 4);
				Test->TestEqual(TEXT("Winner is first in results"), Data.Entries[0].Rank, 1);
				if (Teams == 0 && Case != 5) { Test->TestEqual(TEXT("99 knockouts cannot beat the last survivor"), Match->Royale.Entries[0].Place, 3); }
				Shot(TEXT("Results"));
				if (Flight.IsValid()) { Flight->Destroy(); }
				if (bSingleCase) { return true; }
				if (Case == 5) { PC->GetResultsView()->ShowStats(); NextAt = Now + 1.; Step = 5; return false; }
				++Case;
				Step = 0; bIntroShot = false; bReduced = false; bMidShot = false; bOutShot = false; bHurryShot = false; NextAt = Now + 1.; Flight.Reset(); return false;
			}
			if (Step == 5) { Shot(TEXT("Stats")); return true; }
			return false;
		}
	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds(), NextAt = 0., PhaseStart = 0.;
		int32 Case = 0, Step = 0;
		bool bIntroShot = false, bReduced = false, bMidShot = false, bOutShot = false, bHurryShot = false;
		bool bSingleCase = false;
		TWeakObjectPtr<AChaosImpactBall> Flight;
		FVector LastBallPosition = FVector::ZeroVector;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoyaleFlowTest, "ChaosImpact.BattleRoyale.Flow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRoyaleFlowTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRoyaleFlow(this)); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoyaleRulesTest, "ChaosImpact.BattleRoyale.RulesAndDamage",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRoyaleRulesTest::RunTest(const FString&)
{
	for (int32 Teams = 2; Teams <= 4; ++Teams)
	{
		for (int32 Humans = 1; Humans <= (Teams == 3 ? 6 : 8); ++Humans)
		{
			FChaosImpactMatchRules R; R.bBattleRoyale = true; R.TeamCount = Teams; R.CPUCount = 0;
			R = ChaosImpactMatch::Sanitize(R, Humans);
			TestEqual(TEXT("Teams are equal after CPU autofill"), (Humans + R.CPUCount) % Teams, 0);
			TestTrue(TEXT("Autofill never exceeds eight participants"), Humans + R.CPUCount <= 8);
			FChaosImpactMatchRules Read;
			const FString URLText = FString(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson?")) + ChaosImpactMatch::ToOptions(R);
			const FURL URL(nullptr, *URLText, TRAVEL_Absolute);
			TestTrue(TEXT("Royale options round-trip"), ChaosImpactMatch::ReadOptions(URL, Read) && Read.bBattleRoyale && Read.TeamCount == Teams);
		}
	}
	UWorld* World = nullptr;
	for (const auto& C : GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { World = C.World(); break; } }
	auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match) { AddError(TEXT("Requires a game world")); return false; }
	const auto OldRules = Match->Rules; const bool OldMatch = Match->bVersusMatch; const auto OldPhase = Match->Phase;
	Match->bVersusMatch = true; Match->Rules.bBattleRoyale = true; Match->Rules.bDuel = false; Match->Rules.TeamCount = 0; Match->Phase = EChaosImpactOnlinePhase::Match;
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Victim = World->SpawnActor<AChaosImpactCharacter>(World->GetAuthGameMode()->GetDefaultPawnClassForController(World->GetFirstPlayerController()), FVector(0,0,9000), FRotator::ZeroRotator, Spawn);
	if (!Victim) { AddError(TEXT("Could not create the damage test character")); Match->Rules = OldRules; Match->bVersusMatch = OldMatch; Match->Phase = OldPhase; return false; }
	for (int32 Kind = 1; Kind < ChaosImpactBallTypes::Count; ++Kind)
	{
		const auto Type = static_cast<EChaosImpactBallType>(Kind);
		if (Type == EChaosImpactBallType::Nova) { continue; }
		Victim->ResetForOnlineMatch(FVector(0,0,9000), FRotator::ZeroRotator);
		auto* Ball = World->SpawnActor<AChaosImpactBall>(FVector(0,0,9500), FRotator::ZeroRotator); Ball->SetBallType(Type);
		Ball->Launch(FVector::ForwardVector, 1.f, EChaosImpactBallFlightMode::Straight, 0.f);
		TestTrue(TEXT("A special launch creates its shared damage budget"), Ball->GetDuelThrowDamage().IsValid());
		Victim->TakeDamage(2.f, FDamageEvent(), nullptr, Ball);
		Victim->TakeDamage(1.f, FDamageEvent(), nullptr, Ball);
		TestEqual(TEXT("A special throw cannot remove a second HP from the same opponent"), Victim->GetHealth(), 2.f);
		Ball->Destroy();
	}
	Victim->ResetForOnlineMatch(FVector(0,0,9000), FRotator::ZeroRotator);
	auto* Nova = World->SpawnActor<AChaosImpactBall>(FVector(0,0,9500), FRotator::ZeroRotator); Nova->SetBallType(EChaosImpactBallType::Nova);
	Nova->Launch(FVector::ForwardVector, 1.f, EChaosImpactBallFlightMode::Straight, 0.f);
	Victim->TakeDamage(3.f, FDamageEvent(), nullptr, Nova);
	TestTrue(TEXT("Nova preserves its full knockout power"), Victim->IsEliminated());
	Victim->ResetForOnlineMatch(FVector(0,0,9000), FRotator::ZeroRotator);
	auto* Captor = World->SpawnActor<AChaosImpactCharacter>(Victim->GetClass(), FVector(500,0,9000), FRotator::ZeroRotator, Spawn);
	if (TestNotNull(TEXT("Capture test opponent"), Captor))
	{
		auto* Form = Victim->FindComponentByClass<UChaosImpactBallFormComponent>();
		TestTrue(TEXT("A royale entrant can be captured before the bell"), Form->Capture(Captor));
		Victim->EliminateFromBattleRoyale();
		TestTrue(TEXT("Capture cannot prevent a lower-place sudden death elimination"), Victim->IsEliminated() && !Form->IsInBallForm());
		Captor->Destroy();
	}
	Nova->Destroy(); Victim->Destroy(); Match->Rules = OldRules; Match->bVersusMatch = OldMatch; Match->Phase = OldPhase;
	return true;
}
#endif

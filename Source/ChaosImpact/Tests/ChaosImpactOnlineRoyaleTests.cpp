#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactSpectatorPawn.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	// Real relay host + split-screen guest, -CIAutoVersus=0:1:30:royale -CIMatchSeconds=8.
	class FOnlineRoyaleCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOnlineRoyaleCommand(FAutomationTestBase* T) : Test(T) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 150.) { Test->AddError(FString::Printf(TEXT("Online royale timeout at step %d"), Step)); return true; }
			UWorld* World = nullptr;
			for (const auto& C : GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { World = C.World(); break; } }
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->bOnlineRoom || !World->GetGameInstance()) { return false; }
			const bool bHost = World->GetNetMode() == NM_ListenServer;
			auto* PC = Cast<AChaosImpactPlayerController>(World->GetGameInstance()->GetFirstLocalPlayerController());
			if (!PC) { return false; }
			if (Step == 0)
			{
				if (!Match->IsBattleRoyale() || Match->Phase != EChaosImpactOnlinePhase::Match || Match->Royale.Entries.Num() != 4) { return false; }
				Test->TestEqual(TEXT("Online roster includes host, two guest players and CPU"), Match->Royale.Entries.Num(), 4);
				for (int32 Team = 0; Team < Match->Rules.TeamCount; ++Team)
				{
					int32 Count = 0; for (const auto& E : Match->Royale.Entries) { Count += E.Team == Team ? 1 : 0; }
					Test->TestEqual(TEXT("Online teams have equal numbers"), Count, 2);
				}
				PhaseStart = Match->PhaseStartedAt;
				if (bHost)
				{
					for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
					for (auto& E : Match->Royale.Entries)
					{
						auto* Pawn = Cast<AChaosImpactCharacter>(E.Member->GetPawn());
						Pawn->SetTrainingMenuFrozen(true);
						if (E.Member->IsABot()) { Pawn->EliminateFromBattleRoyale(); }
						else if (E.Member->bRoomHost) { Pawn->PrepareBattleSuddenDeath(); }
					}
				}
				++Step;
			}
			if (Step == 1)
			{
				if (Match->IsTeamBattle())
				{
					if (!Match->Royale.bFinished) { return false; }
					int32 HostTeam = INDEX_NONE;
					for (const auto& E : Match->Royale.Entries) { if (E.Member && E.Member->bRoomHost) { HostTeam = E.Team; } }
					Test->TestTrue(TEXT("The team with two survivors beats the one with one survivor"), Match->Royale.TeamPlaces.IsValidIndex(HostTeam) && Match->Royale.TeamPlaces[HostTeam] == 1);
					Test->TestFalse(TEXT("An online team winner needs no sudden death"), Match->Royale.bSuddenDeath);
					Step = 3; return false;
				}
				if (!Match->Royale.bSuddenDeath) { return false; }
				if (SuddenSeenAt == 0.) { SuddenSeenAt = Now; }
				if (Now - SuddenSeenAt < .5) { return false; }
				Test->TestEqual(TEXT("Sudden death stays in the same online phase"), Match->PhaseStartedAt, PhaseStart);
				Test->TestEqual(TEXT("Only the two tied guests survive"), Match->Royale.AlivePlayers, 2);
				if (!bHost)
				{
					Test->TestEqual(TEXT("Both local guest connections remain"), World->GetGameInstance()->GetNumLocalPlayers(), 2);
					for (const ULocalPlayer* Local : World->GetGameInstance()->GetLocalPlayers())
					{
						const auto* LocalPC = Local->GetPlayerController(World);
						const auto* Pawn = LocalPC ? Cast<AChaosImpactCharacter>(LocalPC->GetPawn()) : nullptr;
						Test->TestTrue(TEXT("Both participants see replicated one-HP finalists"), Pawn && Pawn->GetHealth() == 1.f);
						Test->TestTrue(TEXT("Both participants retain their split views"), FMath::IsNearlyEqual(Local->Size.X, .5f));
					}
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoyaleQA/OnlineSplitSuddenDeath.png")), true, false);
				}
				NextAt = Now + 2.; ++Step; return false;
			}
			if (Step == 2)
			{
				if (Now < NextAt) { return false; }
				if (bHost)
				{
					Test->TestNotNull(TEXT("Eliminated host is watching while guests continue"), Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()));
					for (const auto& E : Match->Royale.Entries)
					{
						if (!E.bOut && E.Member->bSecondOfMachine) { Cast<AChaosImpactCharacter>(E.Member->GetPawn())->EliminateFromBattleRoyale(); }
					}
				}
				++Step;
			}
			if (Step == 3)
			{
				const auto* Results = PC->GetResultsView();
				if (!Results || !Results->IsActive() || Results->GetShowSeconds() < 3.f) { return false; }
				const auto& Data = Results->GetData();
				Test->TestTrue(TEXT("Online results use battle royale placements"), Data.bBattleRoyale && !Data.bTie && Data.Entries.Num() == 4);
				Test->TestEqual(TEXT("Last surviving participant is first"), Data.Entries[0].Rank, 1);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), Match->IsTeamBattle()
					? bHost ? TEXT("RoyaleQA/OnlineTeamHostResults.png") : TEXT("RoyaleQA/OnlineTeamGuestResults.png")
					: bHost ? TEXT("RoyaleQA/OnlineHostResults.png") : TEXT("RoyaleQA/OnlineGuestResults.png")), true, false);
				++Step;
			}
			if (Step == 4)
			{
				if (PC->GetResultsView()->IsActive() || Match->Phase != EChaosImpactOnlinePhase::Lobby) { return false; }
				Test->TestFalse(TEXT("Online results automatically return to recruitment"), Match->bRecruitmentClosed);
				for (const ULocalPlayer* Local : World->GetGameInstance()->GetLocalPlayers())
				{
					const auto* LocalPC = Local->GetPlayerController(World);
					Test->TestNotNull(TEXT("Formerly eliminated participants return as playable characters"), LocalPC ? Cast<AChaosImpactCharacter>(LocalPC->GetPawn()) : nullptr);
				}
				NextAt = Now + (bHost ? 4. : 1.); ++Step;
			}
			return Step >= 5 && Now >= NextAt;
		}
	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds(), NextAt = 0., PhaseStart = 0., SuddenSeenAt = 0.;
		int32 Step = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineRoyaleTest, "ChaosImpact.Online.BattleRoyale",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOnlineRoyaleTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FOnlineRoyaleCommand(this)); return true; }
#endif

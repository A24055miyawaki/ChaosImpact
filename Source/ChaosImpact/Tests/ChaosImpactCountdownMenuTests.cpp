#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * The countdown after the rules: the room's menus (character change, name change, settings) can be opened during it,
	 * backing out of them stays in the room, and when it ends whatever is open closes and the host plays the match
	 * (never watching it, never out of the room). One listen-server process with a stand-in second member:
	 * Lvl_ThirdPerson?listen?CITraining=1?CITargets=0. Pass -CICountdownMenu=character|name|settings for the menu left
	 * open at the end (character by default).
	 */
	class FCountdownMenuCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCountdownMenuCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CICountdownMenu="), Menu);
			// -CICountdownTourney: a tournament (with CPUs), which begins in the lobby itself, not after a move to a level.
			bTourney = FParse::Param(FCommandLine::Get(), TEXT("CICountdownTourney"));
		}

		virtual bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactGameMode* Mode = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			AChaosImpactGameState* Room = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Countdown flow timed out at step %d"), Step));
				return true;
			}
			if (!PC || !Room || Now < NextAt)
			{
				return false;
			}
			const auto Wait = [this, Now](const double Seconds) { ++Step; NextAt = Now + Seconds; return false; };
			const auto Press = [](const FKey& Key)
			{
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			};
			const auto OpenMenu = [this](AChaosImpactPlayerController* Host)
			{
				Host->ResumeGameplay();
				Host->ShowMenuScreen(EChaosImpactScreen::Pause);
				if (Menu == TEXT("name")) { Host->BeginRoomPlayerRename(); }
				else if (Menu == TEXT("settings")) { Host->OpenSettings(); }
				else { Host->BeginRoomCharacterChange(); }
			};
			const EChaosImpactScreen MenuScreen = Menu == TEXT("name") ? EChaosImpactScreen::OnlineName
				: Menu == TEXT("settings") ? EChaosImpactScreen::Settings : EChaosImpactScreen::CharacterSelect;
			switch (Step)
			{
			case 0:
				if (Now - Started < 4.0) { return false; }
				if (!Mode || !PC->IsOnlineRoomHost() || !Room->bOnlineRoom)
				{
					Test->AddError(TEXT("Requires ?listen?CITraining=1."));
					return true;
				}
				Guest = World->SpawnActor<AChaosImpactPlayerState>();
				Guest->SetPlayerName(TEXT("TEST_GUEST"));
				Guest->JoinOrder = 1;
				Room->AddPlayerState(Guest.Get());
				Mode->CloseRecruitment();
				{
					FChaosImpactMatchRules Rules;
					Rules.Minutes = 1;
					Rules.TeamCount = 0;
					Rules.CPUCount = 0;
					Rules.StageIndex = 0;
					if (bTourney)
					{
						Rules.bTournament = true;
						for (int32 Index = 0; Index < 2; ++Index)
						{
							FChaosImpactTournamentCPU CPU;
							CPU.Name = FString::Printf(TEXT("CPU%d"), Index + 1);
							Rules.Tournament.CPUs.Add(CPU);
						}
					}
					Mode->DecideLobbyRules(Rules);
				}
				PC->ResumeGameplay();
				return Wait(1.0);
			case 1:
				// Everyone presses 準備OK: the countdown begins.
				PC->ServerSetReadyForMatch(true);
				Guest->bReadyForMatch = true;
				return Wait(0.6);
			case 2:
				Test->TestTrue(TEXT("The countdown has begun"), Room->Phase == EChaosImpactOnlinePhase::Starting);
				UE_LOG(LogTemp, Display, TEXT("COUNTDOWN started (%s left open at the end)"), *Menu);
				OpenMenu(PC);
				return Wait(0.5);
			case 3:
				Test->TestTrue(FString::Printf(TEXT("The menu opens during the countdown (is %d)"), static_cast<int32>(PC->GetCurrentScreen())),
					PC->GetCurrentScreen() == MenuScreen);
				// Backing out during the countdown: to the room's pause menu, never out of the room.
				Press(EKeys::Escape);
				return Wait(0.35);
			case 4:
				Press(EKeys::Escape);
				return Wait(0.35);
			case 5:
				Test->TestTrue(TEXT("Backing out in the countdown stays in the room"), PC->IsOnlineRoom());
				Test->TestFalse(TEXT("Backing out in the countdown does not watch"), PC->IsSpectating());
				Test->TestTrue(FString::Printf(TEXT("Backing out lands in the room (is %d)"), static_cast<int32>(PC->GetCurrentScreen())),
					PC->GetCurrentScreen() == EChaosImpactScreen::Pause || PC->GetCurrentScreen() == EChaosImpactScreen::Playing);
				// Opened again, and left open as the countdown runs out.
				OpenMenu(PC);
				LobbyWorld = World;
				return Wait(0.3);
			case 6:
				Test->TestTrue(TEXT("The menu is open as the countdown ends"), PC->GetCurrentScreen() == MenuScreen);
				return Wait(0.2);
			case 7:
				// Until the match begins (here, or after the move to its level and the wait for everyone).
				if (!((bTourney ? Room->IsTournament() : Room->bVersusMatch) && Room->Phase != EChaosImpactOnlinePhase::Starting))
				{
					if (World == LobbyWorld && Room->Phase == EChaosImpactOnlinePhase::Starting && PC->GetCurrentScreen() != MenuScreen
						&& !bReported)
					{
						bReported = true;
						Test->AddError(TEXT("The menu closed before the countdown ended"));
					}
					return false;
				}
				return Wait(1.0);
			case 8:
				UE_LOG(LogTemp, Display, TEXT("COUNTDOWN match begun: screen %d, spectating %d, room %d, level %s"),
					static_cast<int32>(PC->GetCurrentScreen()), PC->IsSpectating() ? 1 : 0, PC->IsOnlineRoom() ? 1 : 0, *World->GetMapName());
				Test->TestTrue(TEXT("Still in the room"), PC->IsOnlineRoom());
				Test->TestFalse(TEXT("Playing, not watching"), PC->IsSpectating());
				if (bTourney)
				{
					const AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>();
					UE_LOG(LogTemp, Display, TEXT("COUNTDOWN tournament role %d"), Own ? static_cast<int32>(Own->TournamentRole) : -1);
					Test->TestTrue(TEXT("In the tournament as a player (playing or practising), not watching it"),
						Own && Own->TournamentRole != EChaosImpactTournamentRole::Spectator);
				}
				Test->TestTrue(FString::Printf(TEXT("The menu is closed for the match (is %d)"), static_cast<int32>(PC->GetCurrentScreen())),
					PC->GetCurrentScreen() == EChaosImpactScreen::Playing || PC->GetCurrentScreen() == EChaosImpactScreen::TeamSelect);
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		FString Menu = TEXT("character");
		TWeakObjectPtr<AChaosImpactPlayerState> Guest;
		TWeakObjectPtr<UWorld> LobbyWorld;
		double Started = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		bool bReported = false;
		bool bTourney = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCountdownMenuTest, "ChaosImpact.Online.CountdownMenus",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCountdownMenuTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCountdownMenuCommand(this));
	return true;
}
#endif

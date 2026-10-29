#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UWorld* FindOnlineShotWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	void OnlineShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineUiQA"), Name), true, false);
	}

	/**
	 * The online menus, each in turn, on the title (/Game/ThirdPerson/Lvl_ThirdPerson): VS online / local, players on
	 * this machine, name, password, room name, the room list and the status screen. Saved/OnlineUiQA/menu_*.png.
	 */
	class FOnlineMenuShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOnlineMenuShotsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(TEXT("Timed out"));
				return true;
			}
			UWorld* World = FindOnlineShotWorld();
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!PC || Now < NextAt)
			{
				return false;
			}
			static const EChaosImpactScreen Screens[] = {EChaosImpactScreen::VSSelect, EChaosImpactScreen::OnlinePlayers,
				EChaosImpactScreen::OnlineName, EChaosImpactScreen::OnlinePassword, EChaosImpactScreen::OnlineRoomName,
				EChaosImpactScreen::RoomList, EChaosImpactScreen::OnlineStatus};
			static const TCHAR* Names[] = {TEXT("1_VSSelect"), TEXT("2_Players"), TEXT("3_Name"), TEXT("4_Password"),
				TEXT("5_RoomName"), TEXT("6_RoomList"), TEXT("7_Status")};
			if (Index == 0 && !bShown && Now - StartedAt < 4.0)
			{
				return false;
			}
			if (Index >= UE_ARRAY_COUNT(Screens))
			{
				return true;
			}
			if (!bShown)
			{
				PC->ShowMenuScreen(Screens[Index]);
				bShown = true;
				NextAt = Now + 0.9;
				return false;
			}
			OnlineShot(FString::Printf(TEXT("menu_%s.png"), Names[Index]));
			bShown = false;
			++Index;
			NextAt = Now + 0.3;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Index = 0;
		bool bShown = false;
	};

	/**
	 * The online room as its host sees it (run on a listen server: Lvl_ThirdPerson?listen?CITraining=1 with
	 * -CIAutoLobby=0:1:4): waiting for the rules, the rules decided, 準備OK, the match starting, its opening.
	 * Saved/OnlineUiQA/lobby_*.png.
	 */
	class FOnlineLobbyShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOnlineLobbyShotsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				return true;
			}
			UWorld* World = FindOnlineShotWorld();
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			const AChaosImpactGameState* Room = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!PC || !Room || Now < NextAt)
			{
				return false;
			}
			switch (Step)
			{
			case 0:
				if (!Room->bOnlineRoom || !PC->IsGameplayActive())
				{
					return false;
				}
				NextAt = Now + 1.5;
				++Step;
				return false;
			case 1:
				OnlineShot(TEXT("lobby_1_Waiting.png"));
				++Step;
				return false;
			case 2:
				if (!Room->bRulesDecided)
				{
					// (Rules are only decided for a room of two machines or more: decided here directly.)
					if (AChaosImpactGameMode* Mode = World->GetAuthGameMode<AChaosImpactGameMode>())
					{
						FChaosImpactMatchRules Rules;
						Rules.CPUCount = 1;
						Mode->DecideLobbyRules(Rules);
					}
					return false;
				}
				NextAt = Now + 1.2;
				++Step;
				return false;
			case 3:
				OnlineShot(TEXT("lobby_2_Rules.png"));
				PC->ServerSetReadyForMatch(true);
				NextAt = Now + 0.25;
				++Step;
				return false;
			case 4:
				OnlineShot(TEXT("lobby_3_Ready.png"));
				++Step;
				return false;
			case 5:
				if (Room->Phase != EChaosImpactOnlinePhase::Starting)
				{
					return false;
				}
				NextAt = Now + 0.5;
				++Step;
				return false;
			case 6:
				OnlineShot(TEXT("lobby_4_Starting.png"));
				NextAt = Now + 1.2;
				++Step;
				return false;
			case 7:
				OnlineShot(TEXT("lobby_5_Countdown.png"));
				++Step;
				return false;
			case 8:
				if (Room->Phase != EChaosImpactOnlinePhase::Intro && Room->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				NextAt = Now + 2.0;
				++Step;
				return false;
			case 9:
				OnlineShot(TEXT("lobby_6_Opening.png"));
				++Step;
				return false;
			case 10:
				if (Room->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				NextAt = Now + 3.0;
				++Step;
				return false;
			default:
				OnlineShot(TEXT("lobby_7_Match.png"));
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactOnlineMenuShotsTest, "ChaosImpact.UI.OnlineMenuShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactOnlineMenuShotsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOnlineMenuShotsCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactOnlineLobbyShotsTest, "ChaosImpact.UI.OnlineLobbyShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactOnlineLobbyShotsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOnlineLobbyShotsCommand(this));
	return true;
}
#endif

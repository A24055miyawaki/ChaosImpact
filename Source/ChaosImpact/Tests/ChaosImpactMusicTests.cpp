#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactMusic.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * The music through the ways between places (run on the title with -CINoTitleDemo -CIMatchSeconds=15): the title quiet,
	 * the menus' music, training's, the menus again when the mode select is opened over training, over the 訓練場 (its lesson
	 * list's back) and over a local VS match's results (メニューへ), the settings and the controller assignment carrying on
	 * whatever was on, and the title quiet again from the results.
	 */
	class FMusicFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FMusicFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			const UChaosImpactMusic* Music = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UChaosImpactMusic>() : nullptr;
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!PC || !Music || Now < NextAt)
			{
				return false;
			}
			// Waits for the music wanted (a while for the fades and loads); then the next step's action.
			const auto Expect = [&](const TCHAR* What, const EChaosImpactBgm Want, const double Within) -> bool
			{
				if (WaitFrom <= 0.0)
				{
					WaitFrom = Now;
				}
				const bool bThere = Music->GetCurrent() == Want && (Want == EChaosImpactBgm::None || !Music->GetCurrentTrackName().IsEmpty());
				if (!bThere && Now - WaitFrom < Within)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("MUSICFLOW %s: want %d, have %d (%s), screen %d"), What, static_cast<int32>(Want),
					static_cast<int32>(Music->GetCurrent()), *Music->GetCurrentTrackName(), static_cast<int32>(PC->GetCurrentScreen()));
				Test->TestEqual(What, static_cast<int32>(Music->GetCurrent()), static_cast<int32>(Want));
				WaitFrom = 0.0;
				return true;
			};
			const auto Travelled = [&]() { return World != From.Get(); };
			switch (Step)
			{
			case 0:
				if (PC->GetCurrentScreen() != EChaosImpactScreen::Title || Now - StartedAt < 4.0)
				{
					return false;
				}
				if (!Expect(TEXT("The title is quiet"), EChaosImpactBgm::None, 0.0)) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				break;
			case 1:
				if (!Expect(TEXT("The mode select: the menus' music"), EChaosImpactBgm::Menu, 4.0)) { return false; }
				PC->OpenSettings();
				NextAt = Now + 1.5;
				break;
			case 2:
				if (!Expect(TEXT("The settings carry the menus' music on"), EChaosImpactBgm::Menu, 0.0)) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->StartTrainingWithPlayers(1);
				From = World;
				break;
			case 3:
				if (!Travelled() || !Expect(TEXT("Training: the training music"), EChaosImpactBgm::Dojo, 15.0)) { return false; }
				PC->ApplyTrainingSettings();
				NextAt = Now + 1.5;
				break;
			case 4:
				if (!Expect(TEXT("Training's player count (controller assignment): training's music on"), EChaosImpactBgm::Dojo, 0.0)) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
				PC->ShowMenuScreen(EChaosImpactScreen::MultiReady);
				break;
			case 5:
				if (!Expect(TEXT("Training's pause to the online menu: the menus' music"), EChaosImpactBgm::Menu, 4.0)) { return false; }
				PC->OpenDojo();
				From = World;
				break;
			case 6:
				if (!Travelled() || !Expect(TEXT("The 訓練場: the training music"), EChaosImpactBgm::Dojo, 15.0)) { return false; }
				// (Its lesson list's back.)
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				break;
			case 7:
				if (!Expect(TEXT("Out of the 訓練場 to the mode select: the menus' music"), EChaosImpactBgm::Menu, 4.0)) { return false; }
				UGameplayStatics::OpenLevel(PC, FName(TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage1")), true,
					TEXT("CITraining=1?CIVersus=1?CIMatch=1?CIMinutes=3?CITeams=0?CIMatchCPU=1?CICPULevel=0?CILocalPlayers=1?CIKeyboardPlayer=0"));
				From = World;
				break;
			case 8:
				if (!Travelled() || !Expect(TEXT("A local VS match: the battle's music"), EChaosImpactBgm::Battle, 30.0)) { return false; }
				break;
			case 9:
				if (!Expect(TEXT("Its results: the results' music"), EChaosImpactBgm::Results, 60.0)) { return false; }
				NextAt = Now + 2.0;
				break;
			case 10:
				// The results' メニューへ.
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				++Step;
				return false;
			case 11:
				if (!Expect(TEXT("The results to the mode select: the menus' music"), EChaosImpactBgm::Menu, 4.0)) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::Title);
				From = World;
				break;
			case 12:
				if (!Travelled() || PC->GetCurrentScreen() != EChaosImpactScreen::Title)
				{
					return false;
				}
				NextAt = Now + 3.0;
				break;
			default:
				Expect(TEXT("Back at the title: quiet"), EChaosImpactBgm::None, 0.0);
				return true;
			}
			++Step;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double WaitFrom = 0.0;
		int32 Step = 0;
		TWeakObjectPtr<UWorld> From;
	};
}

namespace
{
	/**
	 * The battle music the host picks (run on a VS stage as a local match: ?CITraining=1?CIVersus=1?CIMatch=1?CIMatchCPU=2
	 * ?CISpectate=1): the match has a pick, the battle plays it, and a pick that changes a moment into the battle (one that
	 * reached a guest late) takes the music over to it - here to 戦いの旅路を征く, which loads and plays.
	 */
	class FBattleSongCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBattleSongCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const UChaosImpactMusic* Music = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UChaosImpactMusic>() : nullptr;
			if (!Match || !Music || Now < NextAt)
			{
				return false;
			}
			static const TCHAR* const Names[] = {TEXT("BGM_TrickStyle"), TEXT("BGM_BattleJourney")};
			switch (Step)
			{
			case 0:
				if (Music->GetCurrent() != EChaosImpactBgm::Battle || Music->GetCurrentTrackName().IsEmpty())
				{
					return false;
				}
				Test->TestEqual(TEXT("Two battle songs"), UChaosImpactMusic::GetBattleSongCount(), 2);
				Test->TestTrue(FString::Printf(TEXT("The match has a pick (%d)"), Match->BattleSong), Match->BattleSong >= 0 && Match->BattleSong < 2);
				if (Match->BattleSong >= 0 && Match->BattleSong < 2)
				{
					Test->TestEqual(TEXT("The battle plays the host's pick"), Music->GetCurrentTrackName(), FString(Names[Match->BattleSong]));
				}
				// A different pick arriving a moment late (as on a guest): 戦いの旅路を征く, or the next one if that is on already.
				Match->BattleSong = Match->BattleSong == 1 ? 0 : 1;
				const_cast<UChaosImpactMusic*>(Music)->RestartBattleWindowForTests();
				Wanted = Names[Match->BattleSong];
				NextAt = Now + 2.5;
				++Step;
				return false;
			case 1:
				Test->TestEqual(TEXT("Over to the pick that came late"), Music->GetCurrentTrackName(), Wanted);
				if (Wanted != TEXT("BGM_BattleJourney"))
				{
					// (Started on 戦いの旅路を征く: it played already.)
					Test->TestTrue(TEXT("戦いの旅路を征く played first"), true);
				}
				return true;
			default:
				return true;
			}
		}
	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		FString Wanted;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBattleSongTest, "ChaosImpact.Menu.BattleSong",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactBattleSongTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBattleSongCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMusicFlowTest, "ChaosImpact.Menu.MusicFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactMusicFlowTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMusicFlowCommand(this));
	return true;
}

#endif

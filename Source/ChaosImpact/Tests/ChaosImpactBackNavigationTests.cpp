#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Backing out of menus never goes further than the screen they were opened from: closing the settings, カラーエディット,
	 * a room's character change or name change lands back where it was opened, and never on the title or the mode select
	 * (from a room: never out of the room). Each is tried with Esc and with a pad's B, once, twice in a row, and twice
	 * in the same frame (as a key can arrive by two ways at once).
	 *  Online: Lvl_ThirdPerson?listen?CITraining=1?CITargets=0 (a room hosted by this process alone).
	 *  Offline: Lvl_ThirdPerson?CITraining=1?CITargets=0.
	 */
	class FBackNavigationCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBackNavigationCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Back navigation timed out at step %d"), Step));
				return true;
			}
			if (!PC || Now < NextAt)
			{
				return false;
			}
			if (Step == 0)
			{
				if (Now - Started < 4.0) { return false; }
				bOnline = PC->IsOnlineRoom();
				UE_LOG(LogTemp, Display, TEXT("BACKNAV: %s"), bOnline ? TEXT("in a room") : TEXT("offline"));
				BuildPlan(PC);
				Step = 1;
			}
			if (!Plan.IsValidIndex(Step - 1))
			{
				PC->ResumeGameplay();
				return true;
			}
			const float Wait = Plan[Step - 1](PC);
			++Step;
			NextAt = Now + Wait;
			return false;
		}

	private:
		using FAction = TFunction<float(AChaosImpactPlayerController*)>;

		static void Press(const FKey& Key, const int32 Times = 1)
		{
			for (int32 Time = 0; Time < Times; ++Time)
			{
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			}
		}

		static FString ScreenName(const EChaosImpactScreen Screen)
		{
			return StaticEnum<EChaosImpactScreen>() ? StaticEnum<EChaosImpactScreen>()->GetNameStringByValue(static_cast<int64>(Screen))
				: FString::FromInt(static_cast<int32>(Screen));
		}

		/** Where the player must be now, and (online) still in the room. */
		void Expect(AChaosImpactPlayerController* PC, const EChaosImpactScreen Wanted, const FString& What) const
		{
			const EChaosImpactScreen Now = PC->GetCurrentScreen();
			Test->TestTrue(FString::Printf(TEXT("%s: on %s (is %s)"), *What, *ScreenName(Wanted), *ScreenName(Now)), Now == Wanted);
			Test->TestTrue(FString::Printf(TEXT("%s: not on the title or the mode select"), *What),
				Now != EChaosImpactScreen::Title && Now != EChaosImpactScreen::ModeSelect || Wanted == EChaosImpactScreen::ModeSelect);
			if (bOnline)
			{
				Test->TestTrue(FString::Printf(TEXT("%s: still in the room"), *What), PC->IsOnlineRoom());
			}
			UE_LOG(LogTemp, Display, TEXT("BACKNAV %s -> %s"), *What, *ScreenName(Now));
		}

		/** Opens Open from Base, backs out with Key (Times presses), and checks it lands on Back. */
		void AddRound(const FString& Name, TFunction<void(AChaosImpactPlayerController*)> ToBase, TFunction<void(AChaosImpactPlayerController*)> Open,
			const EChaosImpactScreen Opened, const EChaosImpactScreen Back, const FKey& Key, const int32 Times)
		{
			const FString What = FString::Printf(TEXT("%s, %s x%d"), *Name, *Key.ToString(), Times);
			Plan.Add([ToBase](AChaosImpactPlayerController* PC) { ToBase(PC); return 0.5f; });
			Plan.Add([this, What](AChaosImpactPlayerController* PC)
			{
				UE_LOG(LogTemp, Display, TEXT("BACKNAV %s: starting from %s"), *What, *ScreenName(PC->GetCurrentScreen()));
				return 0.05f;
			});
			Plan.Add([this, Open, Opened, What](AChaosImpactPlayerController* PC)
			{
				Open(PC);
				Test->TestTrue(FString::Printf(TEXT("%s: opened"), *What), PC->GetCurrentScreen() == Opened);
				return 0.6f;
			});
			Plan.Add([Key, Times](AChaosImpactPlayerController*) { Press(Key, Times); return 0.6f; });
			Plan.Add([this, Back, What](AChaosImpactPlayerController* PC) { Expect(PC, Back, What); return 0.1f; });
		}

		void BuildPlan(AChaosImpactPlayerController* Owner)
		{
			const auto ToPause = [](AChaosImpactPlayerController* PC)
			{
				// (Back to play first: the pause menu only opens from play, not from the mode select a round before.)
				PC->ResumeGameplay();
				if (PC->GetCurrentScreen() != EChaosImpactScreen::Playing)
				{
					PC->ShowMenuScreen(EChaosImpactScreen::Playing);
				}
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
			};
			const auto ToModeSelect = [](AChaosImpactPlayerController* PC) { PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect); };
			const auto Settings = [](AChaosImpactPlayerController* PC) { PC->OpenSettings(); };
			const auto ColourFromPause = [](AChaosImpactPlayerController* PC) { PC->OpenColourEdit(EChaosImpactScreen::Pause); };
			const auto ColourFromModes = [](AChaosImpactPlayerController* PC) { PC->OpenColourEdit(EChaosImpactScreen::ModeSelect); };
			const TArray<TPair<FKey, int32>> Ways = {{EKeys::Escape, 1}, {EKeys::Gamepad_FaceButton_Right, 1}, {EKeys::Escape, 2},
				{EKeys::Gamepad_FaceButton_Right, 2}};
			for (const TPair<FKey, int32>& Way : Ways)
			{
				// (Twice in one frame: the first closes it, the second must not go on from where it lands.)
				AddRound(TEXT("Settings from the pause menu"), ToPause, Settings, EChaosImpactScreen::Settings, EChaosImpactScreen::Pause,
					Way.Key, Way.Value);
				AddRound(TEXT("カラーエディット from the pause menu"), ToPause, ColourFromPause, EChaosImpactScreen::ColourEdit,
					EChaosImpactScreen::Pause, Way.Key, Way.Value);
				if (bOnline)
				{
					// (Both are opened from the room's pause menu, and go back there.)
					AddRound(TEXT("Character change in the room"), ToPause,
						[](AChaosImpactPlayerController* PC) { PC->BeginRoomCharacterChange(); }, EChaosImpactScreen::CharacterSelect,
						EChaosImpactScreen::Pause, Way.Key, Way.Value);
					AddRound(TEXT("Name change in the room"), ToPause,
						[](AChaosImpactPlayerController* PC) { PC->BeginRoomPlayerRename(); }, EChaosImpactScreen::OnlineName,
						EChaosImpactScreen::Pause, Way.Key, Way.Value);
				}
			}
			if (bOnline)
			{
				// The room's rules (the host): back from them, from the stage select inside them, and from their list of
				// kinds of match (twice in one frame: the list closes, the rules stay).
				const auto Play = [](AChaosImpactPlayerController* PC) { PC->ResumeGameplay(); };
				const auto Rules = [](AChaosImpactPlayerController* PC) { PC->OpenMatchRules(EChaosImpactScreen::Playing); };
				for (const TPair<FKey, int32>& Way : Ways)
				{
					AddRound(TEXT("The room's rules"), Play, Rules, EChaosImpactScreen::MatchRules, EChaosImpactScreen::Playing, Way.Key, Way.Value);
					AddRound(TEXT("The stage select in the rules"), [Play, Rules](AChaosImpactPlayerController* PC) { Play(PC); Rules(PC); },
						[](AChaosImpactPlayerController* PC) { PC->OpenStageSelect(); }, EChaosImpactScreen::StageSelect,
						EChaosImpactScreen::MatchRules, Way.Key, 1);
				}
				Plan.Add([Play, Rules](AChaosImpactPlayerController* PC) { Play(PC); Rules(PC); return 0.6f; });
				Plan.Add([](AChaosImpactPlayerController*) { Press(EKeys::Down); return 0.3f; });
				Plan.Add([](AChaosImpactPlayerController*) { Press(EKeys::Enter); return 0.6f; });
				Plan.Add([](AChaosImpactPlayerController*) { Press(EKeys::Escape, 2); return 0.6f; });
				Plan.Add([this](AChaosImpactPlayerController* PC)
				{
					Expect(PC, EChaosImpactScreen::MatchRules, TEXT("The list of kinds of match, Escape x2"));
					return 0.1f;
				});
				Plan.Add([](AChaosImpactPlayerController*) { Press(EKeys::Escape); return 0.6f; });
				Plan.Add([this](AChaosImpactPlayerController* PC) { Expect(PC, EChaosImpactScreen::Playing, TEXT("Then the rules, Escape")); return 0.1f; });
			}
			// From the pause menu, one more back goes to play (never further), and the pause button reopens it.
			Plan.Add([ToPause](AChaosImpactPlayerController* PC) { ToPause(PC); return 0.5f; });
			Plan.Add([](AChaosImpactPlayerController*) { Press(EKeys::Escape); return 0.6f; });
			Plan.Add([this](AChaosImpactPlayerController* PC) { Expect(PC, EChaosImpactScreen::Playing, TEXT("Pause, Esc")); return 0.1f; });
			// Settings opened from the pause menu, then the pause (Start) button: back to play or the pause menu, still here.
			Plan.Add([ToPause](AChaosImpactPlayerController* PC) { ToPause(PC); PC->OpenSettings(); return 0.6f; });
			Plan.Add([](AChaosImpactPlayerController*) { Press(EKeys::Gamepad_Special_Right); return 0.6f; });
			Plan.Add([this](AChaosImpactPlayerController* PC)
			{
				const EChaosImpactScreen Now = PC->GetCurrentScreen();
				Test->TestTrue(FString::Printf(TEXT("Start in the settings stays (is %s)"), *ScreenName(Now)),
					Now == EChaosImpactScreen::Settings || Now == EChaosImpactScreen::Pause || Now == EChaosImpactScreen::Playing);
				if (bOnline) { Test->TestTrue(TEXT("Start in the settings: still in the room"), PC->IsOnlineRoom()); }
				UE_LOG(LogTemp, Display, TEXT("BACKNAV Start in settings -> %s"), *ScreenName(Now));
				return 0.1f;
			});
			// Last (offline): the mode select over a play level is not left for play again, so it goes after everything else.
			if (!bOnline)
			{
				for (const TPair<FKey, int32>& Way : Ways)
				{
					AddRound(TEXT("Settings from the mode select"), ToModeSelect, Settings, EChaosImpactScreen::Settings,
						EChaosImpactScreen::ModeSelect, Way.Key, Way.Value);
					AddRound(TEXT("カラーエディット from the mode select"), ToModeSelect, ColourFromModes, EChaosImpactScreen::ColourEdit,
						EChaosImpactScreen::ModeSelect, Way.Key, Way.Value);
				}
			}
			(void)Owner;
		}

		FAutomationTestBase* Test;
		TArray<FAction> Plan;
		double Started = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		bool bOnline = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBackNavigationTest, "ChaosImpact.UI.BackNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactBackNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBackNavigationCommand(this));
	return true;
}
#endif

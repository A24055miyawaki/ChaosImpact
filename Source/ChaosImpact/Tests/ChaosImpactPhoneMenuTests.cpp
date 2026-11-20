#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactColourEdit.h"
#include "ChaosImpactGallery.h"
#include "ChaosImpactLocalTourney.h"
#include "ChaosImpactTournamentWidget.h"
#include "ChaosImpactWorldMap.h"
#include "ChaosImpactPaintStudio.h"
#include "ChaosImpactRecordBoards.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSettingsScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * スマホ版 menus: every screen opened in turn (as a phone's single player reaches it), each checked to be the screen it
	 * should be (the phone's flow skips the player count and the controller entry), and shot. Run with -CITouch in a window
	 * of a phone's shape (upright or sideways): Saved/PhoneQA/<w>x<h>_NN_name.png.
	 */
	/** The tour running now (its steps can report what they check). */
	FAutomationTestBase* GTourTest = nullptr;

	struct FTourStep
	{
		/** What opens the next screen. */
		TFunction<void(AChaosImpactPlayerController&, UChaosImpactMenuWidget&)> Act;
		/** The screen it must lead to, and the picture's name (empty: no picture). */
		EChaosImpactScreen Want;
		const TCHAR* Name;
		/** Whatever screen it leads to (a tournament's own flow decides). */
		bool bAnyScreen = false;
	};

	class FPhoneMenuTourCommand : public IAutomationLatentCommand
	{
	public:
		FPhoneMenuTourCommand(FAutomationTestBase* InTest, TArray<FTourStep>&& InSteps) : Test(InTest), Steps(MoveTemp(InSteps))
		{
			GTourTest = InTest;
		}

		explicit FPhoneMenuTourCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			GTourTest = InTest;
			using S = EChaosImpactScreen;
			using PCRef = AChaosImpactPlayerController&;
			using MRef = UChaosImpactMenuWidget&;
			Steps = {
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::Title); }, S::Title, TEXT("title")},
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::ModeSelect); }, S::ModeSelect, TEXT("mode")},
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::VSSelect); }, S::VSSelect, TEXT("vs")},
				// The phone's VS local: against CPUs or watching (no player count).
				{[](PCRef PC, MRef) { PC.BeginVersusLocal(); }, S::TrainingSetup, TEXT("vslocal")},
				// Straight to the characters (no controller entry), and 戻る back to it.
				{[](PCRef PC, MRef) { PC.PrepareTrainingControllerAssignment(1); }, S::CharacterSelect, TEXT("characters")},
				// The cards scrolled to the end of the list.
				{[](PCRef, MRef Menu) { Menu.GetCharacterSelect()->ScrollToTile(ChaosImpactRoster::Num() - 1); }, S::CharacterSelect, TEXT("characters_end")},
				{[](PCRef PC, MRef) { PC.CancelCharacterSelection(); }, S::TrainingSetup, nullptr},
				{[](PCRef PC, MRef) { PC.PrepareTrainingControllerAssignment(1); }, S::CharacterSelect, nullptr},
				{[](PCRef PC, MRef) { PC.OpenMatchRules(S::CharacterSelect); }, S::MatchRules, TEXT("rules")},
				{[](PCRef, MRef Menu) { Menu.SetModePickerForTests(true); }, S::MatchRules, TEXT("rules_kinds")},
				{[](PCRef, MRef Menu) { Menu.SetModePickerForTests(false); Menu.SetBallSwitchForTests(true); }, S::MatchRules, TEXT("rules_balls")},
				{[](PCRef PC, MRef Menu) { Menu.SetBallSwitchForTests(false); PC.OpenStageSelect(); }, S::StageSelect, TEXT("stages")},
				// Training: straight to the characters too; 戻る back to the mode select.
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::ModeSelect); PC.BeginTrainingSetup(); }, S::CharacterSelect, nullptr},
				{[](PCRef PC, MRef) { PC.CancelCharacterSelection(); }, S::ModeSelect, nullptr},
				{[](PCRef PC, MRef) { PC.OpenSettings(); }, S::Settings, TEXT("settings")},
				// Every tab; 操作 into the touch settings (and a step back); a change, then もどる asks whether to save.
				{[](PCRef, MRef Menu) { Menu.GetSettingsScreen()->SetTab(UChaosImpactSettingsScreen::ETab::Picture); }, S::Settings, TEXT("set_picture")},
				{[](PCRef, MRef Menu) { Menu.GetSettingsScreen()->SetTab(UChaosImpactSettingsScreen::ETab::Controls); }, S::Settings, TEXT("set_controls")},
				{[](PCRef, MRef Menu)
					{
						UChaosImpactSettingsScreen* Settings = Menu.GetSettingsScreen();
						Settings->SelectRow(Settings->FindRow(UChaosImpactSettingsScreen::RowProfileItem));
						Settings->Activate();
					}, S::Settings, TEXT("set_touch")},
				{[](PCRef, MRef Menu) { Menu.GetSettingsScreen()->StepBack(); }, S::Settings, nullptr},
				{[](PCRef, MRef Menu)
					{
						UChaosImpactSettingsScreen* Settings = Menu.GetSettingsScreen();
						Settings->SetTab(UChaosImpactSettingsScreen::ETab::Other);
						Settings->SelectRow(Settings->FindRow(UChaosImpactSettingsScreen::RowShake));
						Settings->Adjust(1);
					}, S::Settings, TEXT("set_other")},
				{[](PCRef, MRef Menu) { Menu.GetSettingsScreen()->StepBack(); }, S::Settings, nullptr},
				{[](PCRef, MRef Menu)
					{
						// もどる (the cursor on it), the question up.
						UChaosImpactSettingsScreen* Settings = Menu.GetSettingsScreen();
						while (Settings->GetSideCursor() != UChaosImpactSettingsScreen::SideBack) { Settings->MoveSide(1); }
						Settings->ActivateSide();
					}, S::Settings, TEXT("set_ask")},
				{[](PCRef, MRef Menu) { Menu.GetSettingsScreen()->AnswerSave(UChaosImpactSettingsScreen::ESaveAnswer::LeaveWithoutSaving); }, S::ModeSelect, nullptr},
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::ModeSelect); PC.OpenColourEdit(S::ModeSelect); }, S::ColourEdit, TEXT("colour")},
				{[](PCRef, MRef Menu) { Menu.GetColourEdit()->ShowPhonePage(1); }, S::ColourEdit, TEXT("colour_paint")},
				{[](PCRef, MRef Menu) { Menu.GetColourEdit()->ToggleFine(); }, S::ColourEdit, TEXT("colour_fine")},
				{[](PCRef, MRef Menu) { Menu.GetColourEdit()->ToggleFine(); Menu.GetColourEdit()->ShowPhonePage(2); }, S::ColourEdit, TEXT("colour_presets")},
				// ペイント: its screen; then fingers. One on the model draws (a step to take back); one drawing, a second landing
				// at once: that stroke is taken back without a trace (nothing more to take back).
				{[](PCRef, MRef Menu) { Menu.GetColourEdit()->OpenPaint(); }, S::ColourEdit, TEXT("paint")},
				{[](PCRef, MRef Menu)
					{
						UChaosImpactPaintStudio* Studio = Menu.GetColourEdit()->GetStudio();
						const FVector2D C = Studio->GetModelCentreOnScreen();
						Studio->TouchStart(0, C);
						Studio->TouchMove(0, C + FVector2D(0.0, 30.0));
						Studio->TouchEnd(0, C + FVector2D(0.0, 30.0));
						if (!Studio->CanUndoForTests()) { GTourTest->AddError(TEXT("A finger on the model did not draw.")); }
						Studio->Undo();
						Studio->TouchStart(0, C);
						Studio->TouchMove(0, C + FVector2D(0.0, 20.0));
						Studio->TouchStart(1, C + FVector2D(120.0, 0.0));
						Studio->TouchMove(1, C + FVector2D(160.0, 0.0));
						Studio->TouchMove(0, C + FVector2D(-20.0, 20.0));
						Studio->TouchEnd(1, C + FVector2D(160.0, 0.0));
						Studio->TouchMove(0, C + FVector2D(-40.0, 40.0));
						Studio->TouchEnd(0, C + FVector2D(-40.0, 40.0));
						if (Studio->CanUndoForTests()) { GTourTest->AddError(TEXT("Two fingers left a stroke behind.")); }
						Studio->SetFine(true);
					}, S::ColourEdit, TEXT("paint_fine")},
				{[](PCRef, MRef Menu) { Menu.GetColourEdit()->GetStudio()->Finish(false); }, S::ColourEdit, nullptr},
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::VSSelect); PC.BeginLocalTourney(); }, S::TourneyEntry, TEXT("tourney")},
				{[](PCRef, MRef Menu) { Menu.GetTourneyEntry()->ShowPhonePage(2); }, S::TourneyEntry, TEXT("tourney_rules")},
				{[](PCRef, MRef Menu) { Menu.GetTourneyEntry()->ShowPhonePage(0); Menu.GetTourneyEntry()->OpenNamingForTests(); }, S::TourneyEntry,
					TEXT("tourney_name")},
				{[](PCRef, MRef Menu) { Menu.GetTourneyEntry()->CloseNamingForTests(); }, S::TourneyEntry, nullptr},
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::MultiReady); }, S::MultiReady, TEXT("online")},
				{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::ModeSelect); }, S::ModeSelect, nullptr},
			};
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 300.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d."), Index));
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			if (!PC || !Menu || Now < NextAt)
			{
				return false;
			}
			if (!bStarted)
			{
				bStarted = true;
				Test->TestTrue(TEXT("Run as the phone (-CITouch)"), PC->IsPhoneSolo());
			}
			if (Index >= Steps.Num())
			{
				return true;
			}
			const FTourStep& Step = Steps[Index];
			if (!bActed)
			{
				// Do it, and give the screen time to come in.
				Step.Act(*PC, *Menu);
				bActed = true;
				ActedAt = Now;
				NextAt = Now + 1.8;
				return false;
			}
			// (A screen reached through a level's loading: waited for.)
			if (!Step.bAnyScreen && PC->GetCurrentScreen() != Step.Want && Now - ActedAt < 45.0)
			{
				NextAt = Now + 0.25;
				bWaited = true;
				return false;
			}
			if (bWaited)
			{
				// (Come in just now: its entrance first.)
				bWaited = false;
				NextAt = Now + 1.6;
				return false;
			}
			// Where it led, and its picture (then the next step a moment later: never in the frame the picture is taken).
			if (!Step.bAnyScreen)
			{
				Test->TestEqual(FString::Printf(TEXT("Step %d (%s): the screen"), Index, Step.Name ? Step.Name : TEXT("-")),
					static_cast<int32>(PC->GetCurrentScreen()), static_cast<int32>(Step.Want));
			}
			if (Step.Name)
			{
				const FVector2D View = Menu->GetCachedGeometry().GetLocalSize();
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhoneQA"),
					FString::Printf(TEXT("%dx%d_%02d_%s.png"), FMath::RoundToInt(View.X), FMath::RoundToInt(View.Y), Index, Step.Name)), true, false);
			}
			++Index;
			bActed = false;
			NextAt = Now + 0.5;
			return false;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TArray<FTourStep> Steps;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ActedAt = 0.0;
		bool bWaited = false;
		int32 Index = 0;
		bool bActed = false;
		bool bStarted = false;
	};
}

namespace
{
	/** 対戦履歴 and ランキング to show (as the relay would send them). */
	void FillBoardsForTour(UChaosImpactRecordBoards& Boards, const bool bHistory)
	{
		using FGame = UChaosImpactRecordBoards::FGame;
		using FRow = UChaosImpactRecordBoards::FRow;
		static const TCHAR* const Names[] = {TEXT("ドッチ・ボール"), TEXT("Akira"), TEXT("まぐろ大好き"), TEXT("ShadowFox"), TEXT("ゆうき"),
			TEXT("ねこまる"), TEXT("とっても長い名前のプレイヤー")};
		TArray<FGame> Games;
		TArray<FRow> Rows;
		const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
		for (int32 Index = 0; Index < (bHistory ? 50 : 0); ++Index)
		{
			FGame Game;
			Game.At = Now - Index * 1800 - 300;
			Game.Character = Index % 3 == 0 ? 5 : 0;
			Game.Colour = 1;
			Game.Result = Index % 3 == 1 ? TEXT("L") : TEXT("W");
			Game.Score[0] = Game.Result == TEXT("W") ? 2 : 1;
			Game.Score[1] = Game.Result == TEXT("W") ? 1 : 2;
			Game.Opponent = Names[Index % UE_ARRAY_COUNT(Names)];
			Game.bOpponentPhone = Index % 2 == 0;
			Game.OpponentCharacter = (Index * 5) % 12;
			Game.Delta = Game.Result == TEXT("W") ? 15 : -13;
			Game.bRated = true;
			Game.bMeasured = true;
			Game.RateAfter = 1640 + Index;
			Games.Add(Game);
		}
		for (int32 Index = 0; Index < (bHistory ? 0 : 40); ++Index)
		{
			FRow Row;
			Row.Rank = Index + 1;
			Row.Rate = 2200 - Index * 11;
			Row.Name = Index == 11 ? FString(TEXT("Player")) : FString(Names[Index % UE_ARRAY_COUNT(Names)]);
			Row.Games = 30 + Index * 3;
			Row.Wins = Row.Games / 2;
			Row.Losses = Row.Games - Row.Wins;
			Row.Character = (Index * 7) % 12;
			Row.bMe = Index == 11;
			Row.bPhone = Index % 3 == 1;
			Rows.Add(Row);
		}
		UChaosImpactRecordBoards::FMine Mine;
		Mine.bKnown = true;
		Mine.Rank = 12;
		Mine.Rate = 2079;
		Mine.bMeasured = true;
		Mine.Games = 67;
		Mine.Best = 2100;
		Boards.SetForTests(true, Games, Rows, Mine, 84);
	}
}

/** スマホ版 online menus: each one opened in turn (and shot), as for the phone tour. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPhoneOnlineTourTest, "ChaosImpact.Menu.PhoneOnlineTour",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPhoneOnlineTourTest::RunTest(const FString& Parameters)
{
	using S = EChaosImpactScreen;
	using PCRef = AChaosImpactPlayerController&;
	using MRef = UChaosImpactMenuWidget&;
	TArray<FTourStep> Steps = {
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::MultiReady); }, S::MultiReady, TEXT("online")},
		{[](PCRef PC, MRef) { PC.OpenOnlinePrivate(); }, S::OnlinePrivate, TEXT("private")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::OnlinePassword); }, S::OnlinePassword, TEXT("password")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::OnlineRoomName); }, S::OnlineRoomName, TEXT("roomname")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::RoomList); }, S::RoomList, TEXT("roomlist")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::OnlineStatus); }, S::OnlineStatus, TEXT("status")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::MultiReady); PC.OpenOnlineRecord(); }, S::OnlineRecord, TEXT("record")},
		{[](PCRef PC, MRef Menu) { PC.OpenOnlineHistory(true); FillBoardsForTour(*Menu.GetRecordBoards(), true); }, S::OnlineHistory, TEXT("history")},
		{[](PCRef PC, MRef Menu) { PC.ShowMenuScreen(S::MultiReady); PC.OpenOnlineRanking(); FillBoardsForTour(*Menu.GetRecordBoards(), false); },
			S::OnlineRanking, TEXT("ranking")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::MultiReady); PC.OpenGallery(); }, S::Gallery, TEXT("gallery")},
		// (A work's page once the list is in; then the post page.)
		{[](PCRef, MRef Menu) { Menu.GetGallery()->OpenDetail(0); }, S::Gallery, TEXT("gallery_detail")},
		{[](PCRef, MRef Menu) { Menu.GetGallery()->CloseDetail(); Menu.GetGallery()->OpenPost(); }, S::Gallery, TEXT("gallery_post")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::MultiReady); PC.OpenLineCheck(); }, S::LineCheck, TEXT("linecheck")},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::MultiReady); PC.OpenTourneyList(); }, S::TourneyList, TEXT("tourneylist")},
		{[](PCRef PC, MRef) { PC.OpenTourneyCreate(); }, S::TourneyCreate, TEXT("tourneycreate")},
#if !UE_BUILD_SHIPPING
		// A 1v1 found (made up here): its card, both ways.
		{[](PCRef PC, MRef) { if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(&PC)) { Sessions->DevFakeDuelOffer(); } PC.ShowMenuScreen(S::DuelFound); },
			S::DuelFound, TEXT("duelfound")},
#endif
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::ModeSelect); }, S::ModeSelect, nullptr},
	};
	ADD_LATENT_AUTOMATION_COMMAND(FPhoneMenuTourCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPhoneTourneyFlowTest, "ChaosImpact.Menu.PhoneTourneyFlow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPhoneTourneyFlowTest::RunTest(const FString& Parameters)
{
	// スマホ版 ローカルトーナメント: the entry, then (into its level) the call to the player's match, taken by a tap, and the match.
	// Parameters "cpu3": three CPUs (the host's choice for two CPUs comes up), otherwise one.
	using S = EChaosImpactScreen;
	using PCRef = AChaosImpactPlayerController&;
	using MRef = UChaosImpactMenuWidget&;
	const int32 CPUs = Parameters.Contains(TEXT("cpu3")) ? 3 : 1;
	TArray<FTourStep> Steps = {
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::VSSelect); PC.BeginLocalTourney(); }, S::TourneyEntry, TEXT("lt_entry")},
		{[CPUs](PCRef PC, MRef)
			{
				UChaosImpactLocalTourneySubsystem* Data = UChaosImpactLocalTourneySubsystem::Get(&PC);
				Data->CPUs = CPUs;
				PC.StartLocalTourney();
			}, S::TourneySeat, TEXT("lt_seat")},
		{[](PCRef PC, MRef) { PC.PressTourneySeat(true, 0); }, S::Playing, TEXT("lt_match")},
		{[](PCRef, MRef) {}, S::Playing, TEXT("lt_match_later")},
	};
	ADD_LATENT_AUTOMATION_COMMAND(FPhoneMenuTourCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPhoneTourneyPanelsTest, "ChaosImpact.Menu.PhoneTourneyPanels",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPhoneTourneyPanelsTest::RunTest(const FString& Parameters)
{
	// スマホ版 ローカルトーナメント with three CPUs: the bracket as it starts, the menu, and what comes next (a call, or the
	// question about two CPUs' match).
	using S = EChaosImpactScreen;
	using PCRef = AChaosImpactPlayerController&;
	using MRef = UChaosImpactMenuWidget&;
	using EPanel = UChaosImpactTournamentWidget::EPanel;
	TArray<FTourStep> Steps = {
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::VSSelect); PC.BeginLocalTourney(); }, S::TourneyEntry, nullptr},
		{[](PCRef PC, MRef)
			{
				UChaosImpactLocalTourneySubsystem* Data = UChaosImpactLocalTourneySubsystem::Get(&PC);
				Data->CPUs = 3;
				Data->bPlacementMatches = true;
				PC.StartLocalTourney();
			}, S::Playing, TEXT("lt4_start")},
		{[](PCRef PC, MRef) { if (PC.GetTournamentWidget()) { PC.GetTournamentWidget()->OpenPanel(EPanel::Bracket); } }, S::Playing, TEXT("lt4_bracket"), true},
		{[](PCRef PC, MRef) { if (PC.GetTournamentWidget()) { PC.GetTournamentWidget()->OpenPanel(EPanel::Menu); } }, S::Playing, TEXT("lt4_menu"), true},
		{[](PCRef PC, MRef) { if (PC.GetTournamentWidget()) { PC.GetTournamentWidget()->ClosePanel(); } }, S::Playing, TEXT("lt4_next"), true},
		{[](PCRef, MRef) {}, S::Playing, TEXT("lt4_later"), true},
	};
	ADD_LATENT_AUTOMATION_COMMAND(FPhoneMenuTourCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPhoneWorldMapTest, "ChaosImpact.Menu.PhoneWorldMap",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPhoneWorldMapTest::RunTest(const FString& Parameters)
{
	// スマホ版 ソロ: the world map (run on its level, ?CIWorldMap=1): walking, and the whole map.
	using S = EChaosImpactScreen;
	using PCRef = AChaosImpactPlayerController&;
	using MRef = UChaosImpactMenuWidget&;
	TArray<FTourStep> Steps = {
		{[](PCRef, MRef) {}, S::Playing, nullptr},
		{[](PCRef, MRef) {}, S::Playing, TEXT("wm_walk")},
		{[](PCRef PC, MRef) { if (AChaosImpactWorldMap* Map = AChaosImpactWorldMap::Find(&PC)) { Map->SetOverview(true); } }, S::Playing, nullptr},
		{[](PCRef, MRef) {}, S::Playing, TEXT("wm_overview")},
		{[](PCRef PC, MRef) { if (AChaosImpactWorldMap* Map = AChaosImpactWorldMap::Find(&PC)) { Map->SetOverview(false); } }, S::Playing, nullptr},
		{[](PCRef PC, MRef) { PC.ShowMenuScreen(S::Pause); }, S::Pause, TEXT("wm_pause")},
	};
	ADD_LATENT_AUTOMATION_COMMAND(FPhoneMenuTourCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPhoneMenuTourTest, "ChaosImpact.Menu.PhoneTour",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPhoneMenuTourTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPhoneMenuTourCommand(this));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
// 対戦履歴 and ランキング: the relay's answers read, and the pages drawn with made-up games and players (no relay needed), on
// the title (/Game/ThirdPerson/Lvl_ThirdPerson, -CIDevRecords=measured): the online menu with its ランキング tile, 戦績 with its
// two 履歴 buttons, the history of each mode (top and scrolled), the ranking of each mode. Saved/OnlineUiQA/boards_*.png.
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactRecordBoards.h"
#include "ChaosImpactScreen.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace
{
	using FGame = UChaosImpactRecordBoards::FGame;
	using FRow = UChaosImpactRecordBoards::FRow;
	using FMine = UChaosImpactRecordBoards::FMine;

	TSharedPtr<FJsonObject> BoardsJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
		return Out;
	}

	TArray<FGame> FakeGames(const bool bDuel, const int32 Count)
	{
		static const TCHAR* const Names[] = {TEXT("ドッチ・ボール"), TEXT("Akira"), TEXT("まぐろ大好き"), TEXT("ShadowFox"), TEXT("ゆうき"),
			TEXT("ねこまる"), TEXT("Rin"), TEXT("とっても長い名前のプレイヤー")};
		TArray<FGame> Games;
		int32 Rate = 1642;
		const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FGame Game;
			Game.At = Now - Index * 1800 - 300;
			Game.Character = Index % 3 == 0 ? 5 : 0;
			Game.Colour = 1;
			Game.Stage = bDuel ? -1 : Index % 6;
			Game.bRated = Index != 3;
			Game.bMeasured = Index < Count - 5;
			if (bDuel)
			{
				const int32 Roll = (Index * 7) % 5;
				Game.Result = Roll == 4 ? TEXT("D") : Roll % 2 == 0 ? TEXT("W") : TEXT("L");
				Game.Score[0] = Game.Result == TEXT("W") ? 2 : Game.Result == TEXT("D") ? 1 : Index % 2;
				Game.Score[1] = Game.Result == TEXT("L") ? 2 : Game.Result == TEXT("D") ? 1 : Index % 2;
				Game.Opponent = Names[Index % UE_ARRAY_COUNT(Names)];
				Game.OpponentCharacter = (Index * 5) % 12;
				Game.OpponentColour = 0;
				Game.Delta = Game.Result == TEXT("W") ? 14 + Index % 9 : Game.Result == TEXT("L") ? -(12 + Index % 7) : 0;
			}
			else
			{
				Game.Of = 8;
				Game.Place = 1 + (Index * 3) % 8;
				Game.Points = 1800 - Game.Place * 170 + Index * 7;
				Game.Knockouts = 8 - Game.Place;
				Game.Delta = (4 - Game.Place) * 7;
				for (int32 Seat = 0; Seat < 8; ++Seat)
				{
					FGame::FSeat Who;
					Who.Place = Seat + 1;
					Who.Points = 1900 - Seat * 180;
					Who.Character = (Seat * 3 + Index) % 12;
					Who.bCPU = Seat >= 5;
					Who.Name = Who.Place == Game.Place ? FString(TEXT("Player")) : Who.bCPU ? FString::Printf(TEXT("CPU%d"), Seat) : FString(Names[(Seat + Index) % 8]);
					Game.Seats.Add(Who);
				}
			}
			Game.RateAfter = Rate;
			Rate -= Game.Delta;
			Games.Add(Game);
		}
		return Games;
	}

	TArray<FRow> FakeRows(const bool bDuel, const int32 Count, const int32 MeAt)
	{
		static const TCHAR* const Names[] = {TEXT("ドッチ・ボール"), TEXT("Akira"), TEXT("まぐろ大好き"), TEXT("ShadowFox"), TEXT("ゆうき"),
			TEXT("ねこまる"), TEXT("Rin"), TEXT("とっても長い名前のプレイヤー"), TEXT("Kaito"), TEXT("みかん")};
		TArray<FRow> Rows;
		int32 Rank = 1;
		int32 Rate = bDuel ? 2214 : 1987;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FRow Row;
			// Two share the 5th place.
			if (Index > 0 && Index != 5)
			{
				Rate -= 6 + (Index * 13) % 17;
				Rank = Index + 1;
			}
			Row.Rank = Rank;
			Row.Rate = Rate;
			Row.Name = Index == MeAt ? FString(TEXT("Player")) : FString::Printf(TEXT("%s%s"), Names[Index % UE_ARRAY_COUNT(Names)],
				Index >= 10 ? *FString::FromInt(Index) : TEXT(""));
			Row.Games = 20 + (Index * 37) % 180;
			Row.Wins = Row.Games * (60 - Index % 30) / 100;
			Row.Losses = Row.Games - Row.Wins;
			Row.Top3 = Row.Games / 2;
			Row.Character = (Index * 7) % 12;
			Row.Colour = Index % 4;
			Row.bMe = Index == MeAt;
			Rows.Add(Row);
		}
		return Rows;
	}

	void BoardsShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineUiQA"), Name), true, false);
	}

	class FRecordBoardsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRecordBoardsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			if (!PC || !Menu || Now < NextAt || (Step == 0 && Now - StartedAt < 4.0))
			{
				return false;
			}
			UChaosImpactRecordBoards* Boards = Menu->GetRecordBoards();
			// Each step: change something, then (on the next) take the picture once it has settled.
			const auto Pad = [Menu](const FKey& Key, const int32 Times = 1) { for (int32 Press = 0; Press < Times; ++Press) { Menu->Navigate(Key); } };
			const int32 Here = Step++;
			switch (Here)
			{
			case 0:
				PC->ShowMenuScreen(EChaosImpactScreen::MultiReady);
				NextAt = Now + 1.4;
				return false;
			case 1:
			{
				// The pad reaches every tile: 8人 down to 戦績, right to ランキング, right to みんなのカラー; 1v1 down to ランキング.
				Menu->SelectEntry(0);
				Pad(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("8人 -> down -> 戦績"), Menu->GetSelectedIndex(), 3);
				Pad(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("戦績 -> right -> ランキング"), Menu->GetSelectedIndex(), 10);
				Pad(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("ランキング -> right -> みんなのカラー"), Menu->GetSelectedIndex(), 9);
				Pad(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("みんなのカラー -> down -> なまえ"), Menu->GetSelectedIndex(), 4);
				Pad(EKeys::Gamepad_DPad_Left);
				Test->TestEqual(TEXT("なまえ -> left -> 回線チェック"), Menu->GetSelectedIndex(), 8);
				Pad(EKeys::Gamepad_DPad_Up);
				Test->TestEqual(TEXT("回線チェック -> up -> ランキング"), Menu->GetSelectedIndex(), 10);
				Pad(EKeys::Gamepad_DPad_Up);
				Test->TestEqual(TEXT("ランキング -> up -> 1v1"), Menu->GetSelectedIndex(), 1);
				Pad(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("1v1 -> right -> 合言葉"), Menu->GetSelectedIndex(), 2);
				Pad(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("合言葉 -> down -> トーナメント"), Menu->GetSelectedIndex(), 6);
				// Every tile reached from 8人 by some way round.
				TSet<int32> Seen;
				for (const FKey& Key : {EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up})
				{
					for (int32 Start = 0; Start < 11; ++Start)
					{
						Menu->SelectEntry(Start);
						Pad(Key);
						Seen.Add(Menu->GetSelectedIndex());
					}
				}
				Test->TestEqual(TEXT("Every one of the 11 reached by the pad"), Seen.Num(), 11);
				Menu->SelectEntry(10);
				NextAt = Now + 0.5;
				return false;
			}
			case 2:
				BoardsShot(TEXT("boards_1_OnlineMenu.png"));
				NextAt = Now + 0.3;
				return false;
			case 3:
				PC->OpenOnlineRecord();
				NextAt = Now + 1.4;
				return false;
			case 4:
				// 戻る, then the two 履歴 buttons, left to right.
				Test->TestEqual(TEXT("戦績: three buttons"), Menu->GetEntryCount(), 3);
				Menu->SelectEntry(0);
				Pad(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("戻る -> right -> 1v1の履歴"), Menu->GetSelectedIndex(), 1);
				Pad(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("-> right -> 8人の履歴"), Menu->GetSelectedIndex(), 2);
				Pad(EKeys::Gamepad_DPad_Left, 2);
				Test->TestEqual(TEXT("left twice -> 戻る"), Menu->GetSelectedIndex(), 0);
				Pad(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("down -> 1v1の履歴"), Menu->GetSelectedIndex(), 1);
				NextAt = Now + 0.5;
				return false;
			case 5:
				BoardsShot(TEXT("boards_2_Record.png"));
				NextAt = Now + 0.3;
				return false;
			case 6:
				PC->OpenOnlineHistory(true);
				NextAt = Now + 0.4;
				return false;
			case 7:
				Test->TestTrue(TEXT("The history page is open"), Boards && Boards->IsOpen() && Boards->GetBoard() == UChaosImpactRecordBoards::EBoard::History);
				if (!Boards) { return true; }
				Boards->SetForTests(true, FakeGames(true, 50), {}, FMine(), 0);
				Boards->SetForTests(false, FakeGames(false, 23), {}, FMine(), 0);
				NextAt = Now + 2.2;
				return false;
			case 8:
				BoardsShot(TEXT("boards_3_HistoryDuel.png"));
				NextAt = Now + 0.3;
				return false;
			case 9:
				Boards->Select(37);
				Test->TestTrue(TEXT("Scrolled to show the chosen game"), Boards->GetScroll() <= 37 && Boards->GetScroll() + UChaosImpactRecordBoards::HistoryRows > 37);
				NextAt = Now + 1.6;
				return false;
			case 10:
				BoardsShot(TEXT("boards_4_HistoryDuelScrolled.png"));
				NextAt = Now + 0.3;
				return false;
			case 11:
				Boards->SetDuel(false);
				Test->TestEqual(TEXT("The 8人 games"), Boards->GetGames().Num(), 23);
				Boards->Select(1);
				NextAt = Now + 2.0;
				return false;
			case 12:
				BoardsShot(TEXT("boards_5_HistoryFfa.png"));
				NextAt = Now + 0.3;
				return false;
			case 13:
				Boards->SetForTests(false, {}, {}, FMine(), 0);
				NextAt = Now + 0.8;
				return false;
			case 14:
				BoardsShot(TEXT("boards_6_HistoryEmpty.png"));
				NextAt = Now + 0.3;
				return false;
			case 15:
				PC->OpenOnlineRanking();
				NextAt = Now + 0.4;
				return false;
			case 16:
			{
				Test->TestTrue(TEXT("The ranking page is open"), Boards->IsOpen() && Boards->GetBoard() == UChaosImpactRecordBoards::EBoard::Ranking);
				FMine Mine;
				Mine.bKnown = true;
				Mine.bMeasured = true;
				Mine.Rank = 12;
				Mine.Rate = 1642;
				Mine.Best = 1688;
				Mine.Games = 36;
				Boards->SetForTests(true, {}, FakeRows(true, 100, 11), Mine, 84);
				FMine Measuring;
				Measuring.bKnown = true;
				Measuring.Left = 2;
				Measuring.Games = 3;
				Boards->SetForTests(false, {}, FakeRows(false, 2, -1), Measuring, 2);
				NextAt = Now + 3.0;
				return false;
			}
			case 17:
				BoardsShot(TEXT("boards_7_RankingDuel.png"));
				NextAt = Now + 0.3;
				return false;
			case 18:
				Boards->Select(60);
				NextAt = Now + 1.2;
				return false;
			case 19:
				BoardsShot(TEXT("boards_8_RankingScrolled.png"));
				NextAt = Now + 0.3;
				return false;
			case 20:
				Boards->SetDuel(false);
				NextAt = Now + 3.0;
				return false;
			case 21:
				BoardsShot(TEXT("boards_9_RankingFfaFew.png"));
				NextAt = Now + 0.3;
				return false;
			case 22:
				PC->ShowMenuScreen(EChaosImpactScreen::MultiReady);
				NextAt = Now + 0.6;
				return false;
			case 23:
				Test->TestFalse(TEXT("Closed on leaving (its characters gone)"), Boards->IsOpen());
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
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordBoardsReadTest, "ChaosImpact.Online.BoardsRead", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRecordBoardsReadTest::RunTest(const FString&)
{
	// What the relay sends (Tools/OnlineRelay/test_relay_history.py makes the same shapes).
	TArray<FGame> Games;
	UChaosImpactRecordBoards::ReadGames(BoardsJson(TEXT(R"({"ok":true,"mode":"duel","page":0,"pages":1,"total":2,"entries":[
		{"st":-1,"ch":3,"co":1,"sc":[2,1],"m":"x","g":1,"at":1763000000,"op":{"n":"B","ch":5,"co":0},"res":"W","d":18,"r":1532,"ms":true},
		{"st":4,"ch":2,"co":1,"pt":1400,"ko":6,"ps":[["P",2,1,1,1400,0],["CPU1",1,0,2,850,1]],"m":"y","g":0,"at":1763000100,"pl":1,"of":8}]})")), Games);
	TestEqual(TEXT("Two games read"), Games.Num(), 2);
	if (Games.Num() == 2)
	{
		TestEqual(TEXT("Result"), Games[0].Result, FString(TEXT("W")));
		TestEqual(TEXT("Rounds"), Games[0].Score[0] * 10 + Games[0].Score[1], 21);
		TestEqual(TEXT("Opponent"), Games[0].Opponent, FString(TEXT("B")));
		TestEqual(TEXT("Opponent's character"), Games[0].OpponentCharacter, 5);
		TestTrue(TEXT("Rated and measured"), Games[0].bRated && Games[0].bMeasured);
		TestEqual(TEXT("Rating after"), Games[0].RateAfter, 1532);
		TestEqual(TEXT("Place"), Games[1].Place, 1);
		TestEqual(TEXT("Seats"), Games[1].Seats.Num(), 2);
		TestTrue(TEXT("The CPU marked"), Games[1].Seats.Num() == 2 && Games[1].Seats[1].bCPU);
		TestFalse(TEXT("Not settled yet: no rating"), Games[1].bRated);
	}
	TArray<FRow> Rows;
	FMine Mine;
	int32 Total = 0;
	UChaosImpactRecordBoards::ReadRanking(BoardsJson(TEXT(R"({"ok":true,"mode":"duel","page":0,"pages":1,"total":2,
		"entries":[{"k":1,"n":"A","r":1560,"g":6,"c":3,"o":1,"me":false,"w":6,"l":0},{"k":2,"n":"B","r":1440,"g":6,"c":5,"o":0,"me":true,"w":0,"l":6}],
		"me":{"rank":2,"rate":1440,"measured":true,"left":0,"games":6,"best":1500}})")), Rows, Mine, Total);
	TestEqual(TEXT("Rows"), Rows.Num(), 2);
	TestEqual(TEXT("Total"), Total, 2);
	TestTrue(TEXT("Mine"), Mine.bKnown && Mine.bMeasured && Mine.Rank == 2 && Mine.Best == 1500);
	TestTrue(TEXT("Own row"), Rows.Num() == 2 && Rows[1].bMe && Rows[0].Character == 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordBoardsShotsTest, "ChaosImpact.UI.RecordBoardsShots", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRecordBoardsShotsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRecordBoardsCommand(this));
	return true;
}
#endif

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Styling/SlateBrush.h"
#include "UObject/Object.h"
#include "ChaosImpactRecordBoards.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * The online menu's two boards, each a page of its own (kept by the relay, see Tools/OnlineRelay/ci_relay.py):
 *  - 対戦履歴: this player's last 50 games of the ランダムマッチ 1v1 or 8人, a list on the left and the chosen game in detail on
 *    the right (the result, when, where, the characters, the rounds or everyone's places, the rating it moved);
 *  - ランキング: the rated players of 1v1 or 8人, best first: the top three on a podium (their characters filmed), the rest
 *    to 100 in a list, and this player's own place under them.
 * Everything is drawn here (no textures of its own); the characters are the same preview stages the menus use.
 */
UCLASS()
class UChaosImpactRecordBoards : public UObject
{
	GENERATED_BODY()
public:
	enum class EBoard : uint8 { History, Ranking };
	/** Games kept and shown (the relay's HISTORY_KEEP) and ranked players shown (its RANKING_TOP). */
	static constexpr int32 HistoryKeep = 50;
	static constexpr int32 RankingTop = 100;
	static constexpr int32 HistoryRows = 8;
	static constexpr int32 RankingRows = 9;

	/** One game of the 対戦履歴. */
	struct FGame
	{
		int64 At = 0;
		/** 1v1: "W", "L" or "D". */
		FString Result;
		/** 8人: the place out of how many. */
		int32 Place = 0;
		int32 Of = 0;
		/** The stage (-1: the duel's arena). */
		int32 Stage = -1;
		int32 Character = 0;
		int32 Colour = -1;
		/** 1v1: rounds won by this player and by the opponent (Score[0] < 0: not known). */
		int32 Score[2] = {-1, -1};
		FString Opponent;
		/** The opponent played on a phone (else a PC). */
		bool bOpponentPhone = false;
		int32 OpponentCharacter = 0;
		int32 OpponentColour = -1;
		/** 8人: this player's points and knockouts, and everyone's places. */
		int32 Points = 0;
		int32 Knockouts = 0;
		struct FSeat
		{
			FString Name;
			int32 Character = 0;
			int32 Colour = -1;
			int32 Place = 0;
			int32 Points = 0;
			bool bCPU = false;
		};
		TArray<FSeat> Seats;
		/** The rating: moved by Delta to RateAfter (bRated); shown once measured (bMeasured), 計測中 before. */
		bool bRated = false;
		bool bMeasured = false;
		int32 Delta = 0;
		int32 RateAfter = 0;
	};

	/** One player of the ランキング. */
	struct FRow
	{
		int32 Rank = 0;
		FString Name;
		int32 Rate = 0;
		int32 Games = 0;
		int32 Wins = 0;
		int32 Losses = 0;
		int32 Top3 = 0;
		int32 Character = 0;
		int32 Colour = -1;
		bool bMe = false;
		/** Plays on a phone (else a PC). */
		bool bPhone = false;
	};

	/** This player in the ランキング. */
	struct FMine
	{
		bool bKnown = false;
		int32 Rank = 0;
		int32 Rate = 0;
		bool bMeasured = false;
		int32 Left = 0;
		int32 Games = 0;
		int32 Best = 0;
	};

	void Open(AChaosImpactPlayerController* InController, EBoard InBoard, bool bInDuel);
	void Close();
	bool IsOpen() const { return bOpen; }
	EBoard GetBoard() const { return Board; }
	bool IsDuel() const { return bDuel; }
	/** Back was asked for: the menu goes on to the page before (once). */
	bool ConsumeLeave();
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	void HandleMouseMove(const FVector2D& DesignPoint);
	/** The button let go (スマホ版: a tap on a row chooses it; a drag scrolled instead). */
	void HandleMouseUp(const FVector2D& DesignPoint);
	void HandleMouseDown(const FVector2D& DesignPoint, const FKey& Button);
	void HandleWheel(const FVector2D& DesignPoint, float Delta);

	/** 1v1 or 8人 (the history and the ranking both have the two). */
	void SetDuel(bool bInDuel);
	void Select(int32 Index);
	int32 GetSelected() const { return Selected; }
	int32 GetScroll() const { return Scroll; }
	const TArray<FGame>& GetGames() const { return bDuel ? DuelGames : FfaGames; }
	const TArray<FRow>& GetRows() const { return bDuel ? DuelRows : FfaRows; }
	const FMine& GetMine() const { return bDuel ? DuelMine : FfaMine; }
	int32 GetRankedTotal() const { return bDuel ? DuelTotal : FfaTotal; }
	bool IsLoading() const;
	/** The relay's answers read (also for the tests: a page of the history, of the ranking). */
	static void ReadGames(const TSharedPtr<FJsonObject>& Reply, TArray<FGame>& Out);
	static void ReadRanking(const TSharedPtr<FJsonObject>& Reply, TArray<FRow>& OutRows, FMine& OutMine, int32& OutTotal);
	/** Tests: what is shown, without the relay. */
	void SetForTests(bool bForDuel, const TArray<FGame>& Games, const TArray<FRow>& Rows, const FMine& Mine, int32 Total);

private:
	void Request(bool bForDuel, int32 Page);
	/** Rows shown at once (fewer upright). */
	int32 ShownRows() const;
	/** スマホ版: a finger down on the screen (where, and the scroll then); moved, it drags the list. */
	bool bPressing = false;
	bool bDragging = false;
	FVector2D PressPoint = FVector2D::ZeroVector;
	int32 PressScroll = 0;
	void Back();
	void Move(int32 Step);
	void ClampScroll();
	int32 GetCount() const;
	/** Where the list's row Index (its place on screen) is drawn. */
	FBox2D RowRect(int32 Visible) const;
	FBox2D TabRect(int32 Index) const;
	FBox2D BackRect() const;
	FBox2D MineRect() const;
	void ShowPortraits();
	void PaintHistory(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	void PaintRanking(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	/** A ranked player's record in short (games and win rate, or 1st places), the same on the podium and in the list. */
	FString RankingStats(const FRow& Row) const;
	void PaintPortrait(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, int32 Index, const FBox2D& Box,
		float Alpha) const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	bool bOpen = false;
	bool bLeave = false;
	EBoard Board = EBoard::History;
	bool bDuel = true;
	double OpenedAt = 0.0;
	double SwitchedAt = 0.0;
	double SelectedAt = 0.0;
	int32 Selected = 0;
	int32 Scroll = 0;
	int32 Hovered = INDEX_NONE;
	bool bStickHeld = false;
	double StickRepeatAt = 0.0;
	float StickY = 0.0f;
	/** Answers to requests from before the last switch or opening are not wanted. */
	int32 Generation = 0;
	int32 Pending = 0;
	bool bFailed = false;
	bool bLoaded[2] = {false, false};

	TArray<FGame> DuelGames;
	TArray<FGame> FfaGames;
	TArray<FRow> DuelRows;
	TArray<FRow> FfaRows;
	FMine DuelMine;
	FMine FfaMine;
	int32 DuelTotal = 0;
	int32 FfaTotal = 0;

	/** The characters filmed: the history's two (this player, the opponent), the ranking's top three. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> Portraits;
	TArray<FSlateBrush> PortraitBrushes;
	TArray<double> PortraitShownAt;
	TArray<FString> PortraitFor;
	int32 Frame = 0;
};

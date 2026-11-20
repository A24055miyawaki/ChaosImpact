#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
namespace ChaosImpactPaint { struct FHudFill; }
#include "ChaosImpactMatchAnnouncerWidget.generated.h"

class UChaosImpactResultsView;
class UChaosImpactChargeWidget;
class AChaosImpactGameState;

/**
 * VS call-outs that belong to the whole screen rather than to one player: Ready?, GO!, the final
 * countdown and FINISH. Added to the full viewport once per machine, so split screen shows them a single time.
 */
UCLASS()
class UChaosImpactMatchAnnouncerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The VS results this machine shows (podium, numbers, awards). */
	UChaosImpactResultsView* GetResultsView() const { return ResultsView; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactResultsView> ResultsView;
	/** Current P1 room presentation, reused in the full viewport rather than clipped inside P1's half. */
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactChargeWidget> OnlineRoomOverlay;

	/**
	 * クラッシュボール: both teams' counts beside the clock, and under it the gauge from the red goal to the blue
	 * goal: how far each team has pushed (its count) and where the giant ball is now.
	 */
	/** クラウンキープ: under the clock, who wears the crown, their counts left and the count in progress. */
public:
	/** The match clock's slab at the top middle (TopCenter: its anchor), as it is drawn in a match (and in the 配置 editor). */
	static void PaintClockSlab(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements, int32 BaseLayer, const FString& Text,
		const FLinearColor& TextColor, float BladeFill, const ChaosImpactPaint::FHudFill& BladeStyle, bool bHurry);
	/** The duel's scoreboard (the round clock, each side's plate, the round marks), from what it shows. */
	struct FDuelBoardSide { FString Name; FLinearColor Color = FLinearColor::White; int32 Cells = 3; int32 Left = 3; int32 Wins = 0; bool bYou = false; bool bTookRound = false; };
	struct FDuelBoard
	{
		bool bSudden = false;
		bool bHurry = false;
		int32 Round = 1;
		int32 Seconds = 0;
		FDuelBoardSide Plates[2];
		FLinearColor SideColors[2] = {FLinearColor::White, FLinearColor::White};
		bool bShowBalls = false;
		TArray<int32> BallPicks;
		TArray<int32> RoundResults;
	};
	static void PaintDuelBoard(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements, int32 BaseLayer, float Time, const FDuelBoard& Board);
	/** A side's colour on the duel's scoreboard. */
	static FLinearColor GetDuelSideColor(int32 Side);
private:
	void PaintCrownPanel(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		const AChaosImpactGameState& Match, double ServerNow, float Time) const;
	void PaintCrashScoreboard(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		const AChaosImpactGameState& Match, double ServerNow, float Time) const;

	/**
	 * The duel (ランクマッチ's rules): both players either side of the round clock, each with the rounds won and the
	 * health left, and under the clock how every round went so far.
	 */
	void PaintDuelScoreboard(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		const AChaosImpactGameState& Match, float Time) const;
	/**
	 * The duel's ball slot, before the first round: three reels of special balls spin and stop one by one on the kinds
	 * this match is played with, then those three are shown together.
	 */
	void PaintDuelBallSlot(const FGeometry& Center, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		const AChaosImpactGameState& Match, float Time) const;
	/** The duel's side shown on the left: this player's own (side 0 when watching or sharing the screen). */
	int32 GetDuelLeftSide(const AChaosImpactGameState& Match) const;
	/** While a duel is painted: whose (a tournament arena's, for one player's part of the screen). Null: the room's own. */
	mutable const struct FChaosImpactDuelState* PaintingDuel = nullptr;
	mutable const APlayerController* PaintingViewer = nullptr;
	/**
	 * バトルロイヤル: the clock in the middle (red-hot at the end, orange through sudden death), the survivors on its left,
	 * and under it everyone as a tile with their health, going dark with their place as they fall.
	 */
	void PaintRoyaleHUD(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const AChaosImpactGameState& Match) const;
	/** What the royale HUD showed last, so changes play out (a tile breaking, the count flashing, the last two). */
	mutable double RoyaleTrackedStart = -1.0;
	mutable TArray<double> RoyaleOutSeenAt;
	mutable int32 RoyaleShownAlive = -1;
	mutable double RoyaleAliveChangedAt = -100.0;
	mutable int32 RoyaleShownSides = -1;
	mutable double RoyaleLastTwoAt = -100.0;

	/** The call-outs' sounds (the countdown's ticks, GO!, the last minute, FINISH, the podium), as each arrives. */
	void UpdateSounds();
	int32 SoundPhase = -1;
	int32 SoundSecond = MAX_int32;
	double SoundReadyAt = 0.0;
	bool bSoundMinuteCalled = false;
	bool bSoundPodiumShown = false;
	/** The duel's step last heard (ROUND, FIGHT!, the round's end). */
	int32 SoundDuelStep = -1;
	double SoundDuelStepAt = 0.0;
	/** The ball slot's reels heard stopping so far. */
	int32 SoundReelsStopped = 0;
};

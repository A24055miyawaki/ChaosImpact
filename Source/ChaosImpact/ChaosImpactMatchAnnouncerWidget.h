#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactMatchAnnouncerWidget.generated.h"

class UChaosImpactResultsView;
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

	/**
	 * クラッシュボール: both teams' counts beside the clock, and under it the gauge from the red goal to the blue
	 * goal: how far each team has pushed (its count) and where the giant ball is now.
	 */
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

#pragma once

#include "CoreMinimal.h"

class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FGeometry;

/** The training tutorial's steps, in the order they are shown. */
enum class EChaosImpactTutorialStep : uint8
{
	Move,
	PickUp,
	Charge,
	Aim,
	Throw,
	Dash,
	Jump,
	Tackle,
	SwapBall,
	CancelThrow,
	DropBall,
	Count
};

/**
 * Training's tutorial for one local player (kept by their charge widget, so every pane of a split screen and every
 * machine online has its own): the controls one at a time in a card at the top right, each in the keys that player
 * really has set; doing it marks it OK and the next comes in. Also the button hints shown down the left in play.
 */
struct FChaosImpactTutorial
{
	/** Follows what the player does (each frame, from their charge widget). SwappedAt: the last ball swap's time. */
	void Tick(AChaosImpactPlayerController* Controller, double SwappedAt);
	/** The card at the top right (TopOffset: how far down it starts, in HUD units). */
	void Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const AChaosImpactPlayerController* Controller,
		float Scale, float TopOffset, float Alpha, TArray<FBox2D>* OutBoxes = nullptr) const;
	/** Whether the card is on screen now (the button hints stay away meanwhile). */
	bool IsShowing() const { return !bHidden && (bActive || FinishedAt > 0.0); }

	/** The button hints down the left side: the main controls in this player's keys, the ones usable right now lit. */
	static void PaintHints(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const AChaosImpactPlayerController* Controller,
		float Scale, TArray<FBox2D>* OutBoxes = nullptr);

private:
	void Restart(double Now);
	void BeginStep(int32 NewStep, double Now);
	bool IsStepAvailable(const AChaosImpactPlayerController* Controller, int32 InStep) const;

	void PaintBallTip(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, float Scale, float Top, float Alpha) const;

	int32 Step = 0;
	bool bActive = false;
	/** Put away for this visit to training (X / the d-pad's right); coming in again shows it again. */
	bool bHidden = false;
	/** Done this visit: not again until the next. */
	bool bDoneThisVisit = false;
	/** The special balls this player has held this visit, and the one being explained (with when it began). */
	TArray<uint8> SeenBalls;
	TArray<uint8> BallTipQueue;
	uint8 BallTip = 0;
	double BallTipAt = 0.0;
	/** When the current step came in, and when it was done (the OK shown before the next). */
	double StepShownAt = 0.0;
	double OkAt = 0.0;
	/** The last step done: the closing card is shown for a moment. */
	double FinishedAt = 0.0;
	/** The setting's revision this run belongs to (turning the tutorial on again starts it over). */
	int32 Revision = INDEX_NONE;
	float Progress = 0.0f;
	// What was seen last frame.
	FVector LastLocation = FVector::ZeroVector;
	bool bHaveLocation = false;
	float Moved = 0.0f;
	FVector LastAim = FVector::ForwardVector;
	float AimTurned = 0.0f;
	bool bWasCharging = false;
	int32 LastBalls = 0;
	/** A charge that has just ended: when, and with how many balls (a throw takes one a moment later). */
	double ChargeEndedAt = 0.0;
	int32 BallsAtChargeEnd = 0;
	bool bChargeEndPending = false;
};

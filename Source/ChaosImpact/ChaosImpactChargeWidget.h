// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactChargeWidget.generated.h"

class APlayerState;
class UProgressBar;
class UTextBlock;
class UBorder;
class UCanvasPanel;
class USizeBox;

/**
 * Runtime-built gameplay HUD. No Widget Blueprint setup is required.
 * Charge, stamina and ball stock are UMG widgets; the hit flash, respawn notice and
 * KO banner are painted in NativePaint so they can animate freely.
 */
UCLASS()
class UChaosImpactChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	void SetChargeAlpha(float ChargeAlpha);
	void SetCharging(bool bCharging);
	/** Only used to detect damage for the screen-edge hit flash. */
	void SetHealth(float CurrentHealth, float MaxHealth);
	void SetStamina(float CurrentStamina, float MaxStamina);
	/** BallTypes: packed EChaosImpactBallType per slot (ChaosImpactBallTypes::Pack), slot 0 thrown next. */
	void SetBallInventory(int32 CurrentBalls, int32 MaximumBalls, uint8 BallTypes = 0);
	void ShowRespawn(const FString& DefeatedBy, float TotalSeconds);
	void UpdateRespawn(float RemainingSeconds, float TotalSeconds);
	void HideRespawn();
	/** Plays the KO banner on this player's screen after they eliminate someone. */
	void ShowKnockout(const FString& VictimName);
	bool IsAimGuideVisible() const;
	/** The two ball slots trade places after the player swaps hands. */
	void PlayBallSwap();
	/** A spectator's HUD: no HP or ball stock, a 観戦中 bar, and the standings seen from the followed player. */
	void SetSpectatorView(bool bSpectator) { bSpectatorView = bSpectator; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void RefreshSplitScreenDividers();
	void RefreshPersonalAimGuide();
	void RefreshBallPickupAnimation();

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> PersonalAimGuideBars;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> ChargeBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChargeLabel;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ChargeFrame;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProgressBar>> StaminaSegments;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> BallSlots;

	/** Outer box of each slot; moved as a whole by the swap animation. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USizeBox>> BallSlotBoxes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> BallSlotAccents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallIcons;

	/** Small ファイア / アイス caption inside each filled slot. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallTypeLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallSeams;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InventoryCountLabel;

	/** Hidden outside the match phase of a VS match. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> StaminaPanel;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> InventoryPanel;

	/** VS: the "+N" pop-up next to this player's points. */
	int32 LastOwnPoints = 0;
	/** VS: the place shown in the rank badge, and when it last changed (tracked while painting). */
	mutable int32 ShownOwnRank = 0;
	mutable int32 OwnRankDelta = 0;
	mutable double OwnRankChangedAt = -100.0;
	int32 PointsGained = 0;
	double PointsGainedAt = -100.0;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> VerticalDivider;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> VerticalDividerAccent;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> HorizontalDivider;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> HorizontalDividerAccent;

	float Health = 3.0f;
	bool bHealthKnown = false;
	double HitAt = -100.0;

	/** Bottom-left dial: HP arcs around the number, stamina as an open outer ring. */
	float MaxHealthValue = 3.0f;
	float StaminaValue = 5.0f;
	float MaxStaminaValue = 5.0f;
	/** Eases toward StaminaValue so a dash visibly drains its arc. */
	float DisplayedStamina = 5.0f;
	int32 LostHealthSegment = INDEX_NONE;
	int32 SpentStaminaSegment = INDEX_NONE;
	double StaminaSpentAt = -100.0;
	/** Hidden with the rest of the gameplay HUD outside a VS match's play phase. */
	bool bVitalsHidden = false;
	bool bSpectatorView = false;

	int32 CarriedBalls = 0;
	uint8 CarriedBallTypes = 0;
	double BallGainedAt = -100.0;
	int32 BallGainedSlot = INDEX_NONE;
	double BallSwappedAt = -100.0;

	bool bRespawnVisible = false;
	double RespawnShownAt = -100.0;
	float RespawnRemaining = 0.0f;
	float RespawnTotal = 3.0f;
	int32 CountdownNumber = INDEX_NONE;
	double CountdownChangedAt = -100.0;
	FString DefeatedByName;

	double KnockoutAt = -100.0;
	int32 KnockoutStreak = 0;
	FString KnockoutVictim;

	/** When each online room member first appeared, for the member list slide-in. */
	TMap<TWeakObjectPtr<APlayerState>, double> MemberSeenAt;

	/** A small note in the top-right corner: someone joined, left, or switched between playing and watching. */
	struct FRoomToast
	{
		/** Named from the member while they are here (their name arrives a moment after they do). */
		TWeakObjectPtr<APlayerState> Member;
		FString Name;
		FString Message;
		FLinearColor Accent = FLinearColor::White;
		double At = 0.0;
	};
	struct FRoomMemberSeen
	{
		FString Name;
		bool bSpectating = false;
		double FirstSeenAt = 0.0;
		/** Arrived just now: announced a moment later, once their chosen name has come through. */
		bool bJoinPending = false;
	};
	TArray<FRoomToast> RoomToasts;
	TMap<TWeakObjectPtr<APlayerState>, FRoomMemberSeen> RoomMembersSeen;
	/** Members already in the room when this screen arrived are learned quietly, without notes. */
	double RoomWatchStartedAt = 0.0;
	void UpdateRoomToasts(const class AChaosImpactGameState* Room);
	void AddRoomToast(APlayerState* Member, const FString& Name, const TCHAR* Message, const FLinearColor& Accent);
	void PaintRoomToasts(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		float Top) const;

	void PaintOnlineOverlay(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** Names above every character in view, and edge arrows toward nearby characters outside it. */
	void PaintPlayerMarkers(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** HUD size for a view of this size: larger in split screen, where each view is only part of the screen. */
	float GetHudScale(double Width, double Height) const;
	/** Ball stock in the bottom-right corner: shaded balls per type on a slanted plate, with pickup and swap motion. */
	void PaintBallInventory(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** HP and stamina dial in the bottom-left corner of this player's view. */
	void PaintVitals(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** Spectating: what is shown (a player's camera or the free camera) and the camera keys. */
	void PaintSpectatorBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** VS opening banners, match timer and standings, and the results. Drawn in every split-screen view. */
	void PaintVersusMatch(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
};

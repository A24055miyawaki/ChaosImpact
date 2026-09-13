// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactChargeWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;
class UCanvasPanel;

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
	void SetBallInventory(int32 CurrentBalls, int32 MaximumBalls);
	void ShowRespawn(const FString& DefeatedBy, float TotalSeconds);
	void UpdateRespawn(float RemainingSeconds, float TotalSeconds);
	void HideRespawn();
	/** Plays the KO banner on this player's screen after they eliminate someone. */
	void ShowKnockout(const FString& VictimName);
	bool IsAimGuideVisible() const;

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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> BallSlotAccents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallIcons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InventoryCountLabel;

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

	int32 CarriedBalls = 0;
	double BallGainedAt = -100.0;
	int32 BallGainedSlot = INDEX_NONE;

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
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactChargeWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;

/** Runtime-built charge gauge. No Widget Blueprint setup is required. */
UCLASS()
class UChaosImpactChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	void SetChargeAlpha(float ChargeAlpha);
	void SetCharging(bool bCharging);
	void SetStamina(float CurrentStamina, float MaxStamina);
	void SetBallInventory(int32 CurrentBalls, int32 MaximumBalls);

private:
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
	TArray<TObjectPtr<UTextBlock>> BallIcons;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactTrainingArena.generated.h"

class USceneComponent;
class UStaticMeshComponent;

/**
 * Placeable training-stage shell. If none is authored in the training map, the
 * primary player controller creates this default layout around the spawn point.
 * Every native block is exposed as a component, so a derived Blueprint can move,
 * resize or replace individual walls and steps in the editor.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Training Arena"))
class AChaosImpactTrainingArena : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactTrainingArena();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Training Arena")
	int32 GetWallCount() const { return WallBlocks.Num(); }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Training Arena")
	int32 GetStepCount() const { return StepBlocks.Num(); }

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* CreateBlock(const FName& Name, const FVector& Location,
		const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator);
	UStaticMeshComponent* CreateZone(const FName& Name, const FVector& Location,
		const FVector& Size);
	void ApplyArenaColors();
	static void TintBlock(UStaticMeshComponent* Block, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training Arena",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> FloorBlock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training Arena",
		meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<UStaticMeshComponent>> WallBlocks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training Arena",
		meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<UStaticMeshComponent>> StepBlocks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training Arena",
		meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<UStaticMeshComponent>> PlatformBlocks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training Arena",
		meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<UStaticMeshComponent>> ZoneBlocks;
};

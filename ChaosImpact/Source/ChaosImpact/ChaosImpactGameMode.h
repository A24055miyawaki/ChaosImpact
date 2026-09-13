// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChaosImpactGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AChaosImpactGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AChaosImpactGameMode();
	/** Live training changes; these never reload the current level. */
	void SetTrainingLocalPlayerCountLive(int32 DesiredPlayers);
	void SetTrainingCPUCountLive(int32 DesiredCPUCount);

protected:
	virtual void BeginPlay() override;

private:
	void SetupLocalTrainingPlayers();
	/** Re-pairs every active local player after startup or an in-place player-count change. */
	void ApplyTrainingControllerAssignments(int32 DesiredPlayers, int32 KeyboardPlayerIndex);
	void SpawnTrainingCPU(const FVector& Anchor, const FRotator& Facing, int32 CPUIndex);
	void SyncTrainingCPUCount(int32 DesiredCPUCount, const FVector& Anchor, const FRotator& Facing);
	/** Re-pairs after spawning/possession settles; player creation can remap device ids. */
	void ReapplyTrainingControllerAssignments();
	void LogTrainingControllerRouting() const;
	FTimerHandle ControllerReassignTimer;
	int32 TrainingKeyboardPlayerIndex = 0;
	int32 LiveDesiredPlayerCount = INDEX_NONE;
	int32 LiveDesiredCPUCount = INDEX_NONE;
	double LocalPlayerSetupStartedAt = 0.0;
};




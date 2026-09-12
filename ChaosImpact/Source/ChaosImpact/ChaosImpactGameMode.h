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

protected:
	virtual void BeginPlay() override;

private:
	void SetupLocalTrainingPlayers();
	void SpawnTrainingCPU(const FVector& Anchor, const FRotator& Facing);
};




#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactStageBase.generated.h"

/**
 * What the game mode needs from a VS stage: where players appear, where the ball pads go and its middle.
 * Every stage is a square of GetHalfExtent() around its actor location; the game state passes that on so the
 * spectator camera stays over the right area. Subclasses build their own geometry (Versus Stage, Splash Stage).
 */
UCLASS(Abstract)
class AChaosImpactStageBase : public AActor
{
	GENERATED_BODY()

public:
	/** Half the floor width of the standard stage. */
	static constexpr float HalfExtent = 3000.0f;

	/** Half the floor width of this stage (the playable square inside its outer walls). */
	virtual float GetHalfExtent() const { return HalfExtent; }

	/** World positions on the floor surface where players may appear. */
	virtual TArray<FVector> GetSpawnPoints() const { return {}; }
	/** World positions on the floor surface for ball pads. */
	virtual TArray<FVector> GetBallPoints() const { return {}; }
	FVector GetCenter() const { return GetActorLocation(); }

protected:
	/** Stage-relative offsets to world positions. */
	TArray<FVector> ToWorld(const TArray<FVector>& Offsets) const;
};

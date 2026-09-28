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

	/**
	 * A character at Location has left the stage: past the outer walls or below the floor, where it can do
	 * nothing (a bug, a glitch through a wall). The match knocks it out so it respawns on the stage.
	 */
	virtual bool IsOutside(const FVector& Location) const;

protected:
	/** How far past the walls still counts as inside (the walls' own thickness and then some). */
	static constexpr float OutsideMargin = 300.0f;
	/** How far below the floor still counts as inside (a character's middle stands about 96 above it). */
	static constexpr float OutsideDepth = 500.0f;

	/** Stage-relative offsets to world positions. */
	TArray<FVector> ToWorld(const TArray<FVector>& Offsets) const;
};

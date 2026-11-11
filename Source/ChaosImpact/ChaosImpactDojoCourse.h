#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactDojoCourse.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * 訓練場's drive course: a narrow winding road with a wall on each side, laid on the stage for a challenge (steer a drive
 * ball down it without touching a wall). The road is a lane on the floor; the walls block like any wall.
 */
UCLASS()
class AChaosImpactDojoCourse : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactDojoCourse();

	/** Lays the road along Path (its points at the road's level, in order) HalfWidth either side of it, its walls from
	 *  Mouth along it; the walls stand from BaseZ (the lowest floor under it) so the raised road reads as one piece. */
	void Build(const TArray<FVector>& Path, float HalfWidth, float Mouth, float BaseZ);

	/** How far Point (flat) is from the road's middle line, and how far along the road its nearest point is. */
	static float DistanceToPath(const TArray<FVector>& Path, const FVector& Point, float& OutAlong);
	static float PathLength(const TArray<FVector>& Path);
	/** The point Distance along the road's middle line (its end past the last point). */
	static FVector PointAlong(const TArray<FVector>& Path, float Distance);

	/** Above the road (low, so the lane stays in sight from the camera's angle). */
	static constexpr float WallHeight = 55.0f;
	static constexpr float WallThickness = 26.0f;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Lane;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Walls;

	void AddWall(const FVector& From, const FVector& To, float Z, float BaseZ, const FLinearColor& Color);
};

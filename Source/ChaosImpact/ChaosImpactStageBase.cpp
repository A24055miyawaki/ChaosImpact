#include "ChaosImpactStageBase.h"

TArray<FVector> AChaosImpactStageBase::ToWorld(const TArray<FVector>& Offsets) const
{
	TArray<FVector> Points;
	Points.Reserve(Offsets.Num());
	for (const FVector& Offset : Offsets)
	{
		Points.Add(GetActorTransform().TransformPosition(Offset));
	}
	return Points;
}

bool AChaosImpactStageBase::IsOutside(const FVector& Location) const
{
	// The playable square around the stage, with the floor at the actor's height.
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	const float Reach = GetHalfExtent() + OutsideMargin;
	return FMath::Abs(Local.X) > Reach || FMath::Abs(Local.Y) > Reach || Local.Z < -OutsideDepth;
}

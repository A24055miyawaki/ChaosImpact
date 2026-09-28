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

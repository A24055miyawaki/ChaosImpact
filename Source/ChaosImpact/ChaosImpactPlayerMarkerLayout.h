#pragma once

#include "CoreMinimal.h"

namespace ChaosImpactPlayerMarkerLayout
{
	// Keep the original ray toward the target while reserving room for the shared duel HUD above it.
	inline FVector2D EdgeTip(const FVector2D& Size, const FVector2D& Direction, const float Scale, const float TopInset)
	{
		const FVector2D Center = Size * .5;
		const double Margin = 30. * Scale + 6.;
		const FVector2D Half(FMath::Max(Center.X - Margin, 1.), FMath::Max(Center.Y - Margin, 1.));
		double Reach = FMath::Min(
			FMath::Abs(Direction.X) > UE_KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Direction.X) : UE_BIG_NUMBER,
			FMath::Abs(Direction.Y) > UE_KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Direction.Y) : UE_BIG_NUMBER);
		Reach += 14. * Scale;
		if (TopInset > 0.f && Direction.Y < -UE_KINDA_SMALL_NUMBER)
		{
			Reach = FMath::Min(Reach, FMath::Max(0., Center.Y - TopInset - 3. * Scale) / -Direction.Y);
		}
		return Center + Direction * Reach;
	}
}

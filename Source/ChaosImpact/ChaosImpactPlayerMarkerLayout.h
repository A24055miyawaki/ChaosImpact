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

	/**
	 * EdgeTip, then drawn in along the same ray until the marker (a box HalfExtent round the point Back behind the tip)
	 * is clear of every HUD area in Keep: the direction stays true, the arrow is never under the HUD.
	 */
	inline FVector2D EdgeTipClear(const FVector2D& Size, const FVector2D& Direction, const float Scale, const float TopInset,
		const TConstArrayView<FBox2D> Keep, const FVector2D& HalfExtent, const float Back)
	{
		const FVector2D Center = Size * .5;
		const FVector2D Edge = EdgeTip(Size, Direction, Scale, TopInset);
		double Reach = (Edge - Center).Size();
		const double Least = FMath::Min(Reach, FMath::Max(HalfExtent.X, HalfExtent.Y) * 2.0 + Back);
		for (; Reach > Least; Reach -= 6.0)
		{
			const FVector2D Body = Center + Direction * (Reach - Back);
			const FBox2D Mark(Body - HalfExtent, Body + HalfExtent);
			bool bClear = true;
			for (const FBox2D& Box : Keep)
			{
				if (Box.bIsValid && Box.Intersect(Mark)) { bClear = false; break; }
			}
			if (bClear) { break; }
		}
		return Center + Direction * FMath::Max(Reach, Least);
	}
}

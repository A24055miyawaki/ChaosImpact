#pragma once
#include "CoreMinimal.h"

namespace ChaosImpactEasyControls
{
	constexpr double DropHoldSeconds = 0.7;
	constexpr double DoubleClickSeconds = 0.25;
	constexpr double DoubleClickRadius = 24.0;
	enum class EManageResult : uint8 { None, Swap, Drop };
	/** A hold drops exactly once; its release never swaps. */
	struct FManageGesture
	{
		bool bHeld = false;
		bool bDropped = false;
		double StartedAt = 0.0;
		void Reset() { *this = FManageGesture(); }
		EManageResult Update(bool bDown, double Now)
		{
			if (bDown && !bHeld) { bHeld = true; bDropped = false; StartedAt = Now; }
			if (bHeld && bDown && !bDropped && Now - StartedAt >= DropHoldSeconds)
			{
				bDropped = true;
				return EManageResult::Drop;
			}
			if (!bDown && bHeld)
			{
				const bool bDrop = !bDropped && Now - StartedAt >= DropHoldSeconds;
				const bool bSwap = !bDropped && !bDrop;
				Reset();
				return bDrop ? EManageResult::Drop : bSwap ? EManageResult::Swap : EManageResult::None;
			}
			return EManageResult::None;
		}
	};
	struct FMoveClicks
	{
		double LastAt = -100.0;
		FVector2D LastPosition = FVector2D::ZeroVector;
		void Reset() { *this = FMoveClicks(); }
		bool Press(const FVector2D& Position, double Now)
		{
			const bool bDouble = Now >= LastAt && Now - LastAt <= DoubleClickSeconds
				&& FVector2D::DistSquared(Position, LastPosition) <= FMath::Square(DoubleClickRadius);
			LastAt = bDouble ? -100.0 : Now;
			LastPosition = Position;
			return bDouble;
		}
	};
}

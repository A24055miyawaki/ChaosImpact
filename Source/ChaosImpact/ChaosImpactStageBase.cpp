#include "ChaosImpactStageBase.h"

namespace
{
	TArray<FVector2D> BuildRoutePolyline2D(const FVector& StartOffset, const FVector& GoalOffset,
		const TArray<FVector>& CustomWaypoints)
	{
		TArray<FVector2D> Polyline;
		Polyline.Reserve(CustomWaypoints.Num() + 2);
		const FVector2D Start2D(StartOffset.X, StartOffset.Y);
		const FVector2D Goal2D(GoalOffset.X, GoalOffset.Y);
		Polyline.Add(Start2D);
		for (const FVector& Waypoint : CustomWaypoints)
		{
			const FVector2D Pt(Waypoint.X, Waypoint.Y);
			if (FVector2D::DistSquared(Pt, Polyline.Last()) > 25.0f && FVector2D::DistSquared(Pt, Goal2D) > 25.0f)
			{
				Polyline.Add(Pt);
			}
		}
		if (FVector2D::DistSquared(Goal2D, Polyline.Last()) > 1.0f)
		{
			Polyline.Add(Goal2D);
		}
		return Polyline;
	}

	float EvaluatePolylineProgress2D(const TArray<FVector2D>& Polyline, const FVector2D& Ball2D,
		const FRotator& TargetGoalRotation, const float MaxLateralDistance, FVector2D* OutTangent = nullptr)
	{
		if (Polyline.Num() < 2)
		{
			if (OutTangent)
			{
				*OutTangent = FVector2D(1.0f, 0.0f);
			}
			return 0.0f;
		}

		const FVector2D Start2D = Polyline[0];
		const FVector2D Goal2D = Polyline.Last();
		const FVector2D OverallDir = (Goal2D - Start2D).GetSafeNormal();
		if (OutTangent)
		{
			*OutTangent = OverallDir;
		}

		// Must be in the target half (ahead of the center line along the overall center-to-goal axis).
		const float SignedForwardFromCenter = FVector2D::DotProduct(Ball2D - Start2D, OverallDir);
		if (SignedForwardFromCenter <= 1.0f)
		{
			if (OutTangent && Polyline.Num() >= 2)
			{
				*OutTangent = (Polyline[1] - Polyline[0]).GetSafeNormal();
			}
			return 0.0f;
		}

		float TotalLength = 0.0f;
		TArray<float, TInlineAllocator<8>> SegmentLengths;
		for (int32 i = 0; i < Polyline.Num() - 1; ++i)
		{
			const float Len = FVector2D::Distance(Polyline[i], Polyline[i + 1]);
			SegmentLengths.Add(Len);
			TotalLength += Len;
		}
		if (TotalLength <= 1.0f)
		{
			return 0.0f;
		}

		float BestDistSq = MAX_flt;
		float BestArc = 0.0f;
		FVector2D BestTangent = OverallDir;
		float AccumulatedArc = 0.0f;
		for (int32 i = 0; i < Polyline.Num() - 1; ++i)
		{
			const FVector2D A = Polyline[i];
			const FVector2D B = Polyline[i + 1];
			const FVector2D AB = B - A;
			const float SegLen = SegmentLengths[i];
			const float SegLenSq = SegLen * SegLen;
			const float T = SegLenSq > 1.0f
				? FMath::Clamp(FVector2D::DotProduct(Ball2D - A, AB) / SegLenSq, 0.0f, 1.0f)
				: 0.0f;
			const FVector2D Closest = A + AB * T;
			const float DistSq = FVector2D::DistSquared(Ball2D, Closest);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestArc = AccumulatedArc + SegLen * T;
				// Look slightly ahead to the next segment when near a waypoint so push direction turns smoothly.
				if (T > 0.8f && i + 2 < Polyline.Num())
				{
					BestTangent = (Polyline[i + 2] - Ball2D).GetSafeNormal();
				}
				else
				{
					BestTangent = AB.GetSafeNormal();
				}
			}
			AccumulatedArc += SegLen;
		}

		if (OutTangent)
		{
			// When approaching the final segment, steer directly toward the goal mouth center.
			if (BestArc > TotalLength * 0.65f)
			{
				const FVector2D ToGoal = (Goal2D - Ball2D).GetSafeNormal();
				if (!ToGoal.IsNearlyZero())
				{
					BestTangent = ToGoal;
				}
			}
			*OutTangent = BestTangent;
		}

		float Alpha = FMath::Clamp(BestArc / TotalLength, 0.0f, 1.0f);

		// Lateral corridor check: moving purely sideways from center or far outside the route corridor does not advance progress.
		const float LateralDist = FMath::Sqrt(FMath::Max(0.0f, BestDistSq));
		const float SafeMaxLat = FMath::Max(400.0f, MaxLateralDistance);
		if (LateralDist > SafeMaxLat)
		{
			const float ExcessPenalty = FMath::Clamp(1.0f - (LateralDist - SafeMaxLat) / 1000.0f, 0.0f, 1.0f);
			Alpha *= ExcessPenalty;
		}

		// Prevent sneaking around the side/back of the goal from falsely advancing the count:
		// GoalForward2D points out of the goal mouth toward the stage center.
		const FVector GoalForward3D = TargetGoalRotation.Vector();
		const FVector2D GoalForward2D = FVector2D(GoalForward3D.X, GoalForward3D.Y).GetSafeNormal();
		if (!GoalForward2D.IsNearlyZero())
		{
			const FVector2D RelToGoal = Ball2D - Goal2D;
			const float InFrontOfGoal = FVector2D::DotProduct(RelToGoal, GoalForward2D);
			const float LateralFromGoalMouth = FMath::Abs(FVector2D::CrossProduct(RelToGoal, GoalForward2D));
			if (InFrontOfGoal < 0.0f && LateralFromGoalMouth > 210.0f)
			{
				// Behind the goal line but outside the goal posts -> clamp progress so flanking behind the goal never cheats the count.
				Alpha = FMath::Min(Alpha, 0.84f);
			}
			else if (InFrontOfGoal < 140.0f && LateralFromGoalMouth > 260.0f)
			{
				// Right beside the goal posts -> cap progress slightly until aligned with the goal opening.
				const float SideCap = FMath::GetMappedRangeValueClamped(
					FVector2D(260.0f, 900.0f), FVector2D(0.95f, 0.80f), LateralFromGoalMouth);
				Alpha = FMath::Min(Alpha, SideCap);
			}
		}

		return FMath::Clamp(Alpha, 0.0f, 1.0f);
	}
}

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

FVector AChaosImpactStageBase::GetCrashBallStartFloorLocation() const
{
	return GetActorTransform().TransformPosition(CrashBallStartOffset);
}

FTransform AChaosImpactStageBase::GetCrashBallGoalTransform(const int32 TeamIndex) const
{
	const FVector LocalOffset = TeamIndex == 0 ? Team0GoalOffset : Team1GoalOffset;
	const FRotator LocalRot = TeamIndex == 0 ? Team0GoalRotation : Team1GoalRotation;
	const FTransform LocalTransform(LocalRot, LocalOffset, FVector::OneVector);
	return LocalTransform * GetActorTransform();
}

void AChaosImpactStageBase::ComputeCrashBallCounts(const FVector& WorldBallLocation, int32& OutTeam0AttackCount,
	int32& OutTeam1AttackCount, float& OutSignedProgress) const
{
	OutTeam0AttackCount = 100;
	OutTeam1AttackCount = 100;
	OutSignedProgress = 0.0f;

	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(WorldBallLocation);
	const FVector2D Ball2D(Local.X, Local.Y);

	const TArray<FVector2D> Route0 = BuildRoutePolyline2D(CrashBallStartOffset, Team1GoalOffset, Team0AttackWaypoints);
	const TArray<FVector2D> Route1 = BuildRoutePolyline2D(CrashBallStartOffset, Team0GoalOffset, Team1AttackWaypoints);

	const float Progress0 = EvaluatePolylineProgress2D(Route0, Ball2D, Team1GoalRotation, CrashBallRouteMaxLateralDistance);
	const float Progress1 = EvaluatePolylineProgress2D(Route1, Ball2D, Team0GoalRotation, CrashBallRouteMaxLateralDistance);

	if (Progress0 > Progress1 && Progress0 > 0.005f)
	{
		OutTeam0AttackCount = FMath::Clamp(100 - FMath::FloorToInt(Progress0 * 99.0f + 0.001f), 1, 100);
		OutTeam1AttackCount = 100;
		OutSignedProgress = Progress0;
	}
	else if (Progress1 > Progress0 && Progress1 > 0.005f)
	{
		OutTeam1AttackCount = FMath::Clamp(100 - FMath::FloorToInt(Progress1 * 99.0f + 0.001f), 1, 100);
		OutTeam0AttackCount = 100;
		OutSignedProgress = -Progress1;
	}
}

FVector AChaosImpactStageBase::GetCrashBallPushDirection(const int32 AttackingTeam, const FVector& WorldBallLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(WorldBallLocation);
	const FVector2D Ball2D(Local.X, Local.Y);

	const FVector TargetGoalOffset = AttackingTeam == 0 ? Team1GoalOffset : Team0GoalOffset;
	const FRotator TargetGoalRot = AttackingTeam == 0 ? Team1GoalRotation : Team0GoalRotation;
	const TArray<FVector>& Waypoints = AttackingTeam == 0 ? Team0AttackWaypoints : Team1AttackWaypoints;

	const TArray<FVector2D> Route = BuildRoutePolyline2D(CrashBallStartOffset, TargetGoalOffset, Waypoints);
	FVector2D Tangent2D(AttackingTeam == 0 ? 1.0f : -1.0f, 0.0f);
	EvaluatePolylineProgress2D(Route, Ball2D, TargetGoalRot, CrashBallRouteMaxLateralDistance, &Tangent2D);

	// If the ball is currently in the defending half (behind center), steer toward the first route segment / center gate first.
	const FVector2D Start2D(CrashBallStartOffset.X, CrashBallStartOffset.Y);
	const FVector2D Goal2D(TargetGoalOffset.X, TargetGoalOffset.Y);
	const FVector2D OverallDir = (Goal2D - Start2D).GetSafeNormal();
	if (FVector2D::DotProduct(Ball2D - Start2D, OverallDir) < -180.0f)
	{
		const FVector2D ToCenterOrGoal = ((Start2D + OverallDir * 350.0f) - Ball2D).GetSafeNormal();
		if (!ToCenterOrGoal.IsNearlyZero())
		{
			Tangent2D = ToCenterOrGoal;
		}
	}

	const FVector WorldDir = GetActorTransform().TransformVectorNoScale(FVector(Tangent2D.X, Tangent2D.Y, 0.0f)).GetSafeNormal2D();
	return !WorldDir.IsNearlyZero() ? WorldDir : FVector(AttackingTeam == 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
}

bool AChaosImpactStageBase::IsOutside(const FVector& Location) const
{
	// The playable square around the stage, with the floor at the actor's height.
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	const float Reach = GetHalfExtent() + OutsideMargin;
	return FMath::Abs(Local.X) > Reach || FMath::Abs(Local.Y) > Reach || Local.Z < -OutsideDepth;
}

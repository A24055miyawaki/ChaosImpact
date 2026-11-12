#include "ChaosImpactStageBase.h"
#include "ChaosImpact.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "ChaosImpactBallTypes.h"
#include "Engine/StaticMesh.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

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

AChaosImpactStageBase::AChaosImpactStageBase()
{
	// The stage effects' ring of light (a flat band) and its glowing material (Color, Intensity).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FxRing(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FxGlow(TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));
	FxRingMesh = FxRing.Succeeded() ? FxRing.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FxSegment(TEXT("/Engine/BasicShapes/Cube.Cube"));
	FxSegmentMesh = FxSegment.Succeeded() ? FxSegment.Object : nullptr;
	FxGlowMaterial = FxGlow.Succeeded() ? FxGlow.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallFade(TEXT("/Game/ChaosImpact/Versus/M_CI_WallFade.M_CI_WallFade"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallFadeGlow(
		TEXT("/Game/ChaosImpact/Versus/M_CI_WallFadeGlow.M_CI_WallFadeGlow"));
	WallFadeMaterial = WallFade.Succeeded() ? WallFade.Object : nullptr;
	WallFadeGlowMaterial = WallFadeGlow.Succeeded() ? WallFadeGlow.Object : nullptr;
}

void AChaosImpactStageBase::MakeWallsSeeThrough(const TArray<FString>& Prefixes, const UMaterialInterface* GlowSource)
{
	if (!WallFadeMaterial || !WallFadeGlowMaterial)
	{
		return;
	}
	TArray<UStaticMeshComponent*> Blocks;
	GetComponents(Blocks);
	for (UStaticMeshComponent* Block : Blocks)
	{
		const FString BlockName = Block->GetName();
		if (!Prefixes.ContainsByPredicate([&BlockName](const FString& Prefix) { return BlockName.StartsWith(Prefix); }))
		{
			continue;
		}
		// (Already see-through, maybe under the palette's own instance of it.)
		const UMaterialInterface* Current = Block->GetMaterial(0);
		const UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Current);
		const UMaterialInterface* Source = Instance ? Instance->Parent.Get() : Current;
		if (Source == WallFadeMaterial || Source == WallFadeGlowMaterial)
		{
			continue;
		}
		Block->SetMaterial(0, GlowSource && Source == GlowSource ? WallFadeGlowMaterial.Get() : WallFadeMaterial.Get());
	}
	// Which side of the arena each block is on (its outward direction from the stage's middle, square to a side, or
	// diagonal at a corner): the material fades only the blocks on the camera's side.
	int32 Sided = 0;
	for (UStaticMeshComponent* Block : Blocks)
	{
		const UMaterialInterface* Current = Block->GetMaterial(0);
		const UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Current);
		const UMaterialInterface* Source = Instance ? Instance->Parent.Get() : Current;
		if (Source != WallFadeMaterial && Source != WallFadeGlowMaterial)
		{
			continue;
		}
		const FVector Centre = Block->GetStaticMesh() ? Block->CalcBounds(Block->GetComponentTransform()).Origin : Block->GetComponentLocation();
		const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Centre);
		const float AX = FMath::Abs(static_cast<float>(Local.X)), AY = FMath::Abs(static_cast<float>(Local.Y));
		FVector2D Out(AX >= AY ? FMath::Sign(Local.X) : 0.0, AY >= AX ? FMath::Sign(Local.Y) : 0.0);
		if (FMath::Min(AX, AY) > FMath::Max(AX, AY) * 0.8f)
		{
			// A corner: both ways.
			Out = FVector2D(FMath::Sign(Local.X), FMath::Sign(Local.Y)).GetSafeNormal();
		}
		const FVector World = GetActorTransform().TransformVectorNoScale(FVector(Out.X, Out.Y, 0.0));
		Block->SetCustomPrimitiveDataFloat(0, static_cast<float>(World.X));
		Block->SetCustomPrimitiveDataFloat(1, static_cast<float>(World.Y));
		++Sided;
		UE_LOG(LogChaosImpact, Verbose, TEXT("See-through wall %s: local %s, outward %s"), *Block->GetName(), *Local.ToString(), *World.ToString());
	}
	if (HasActorBegunPlay() || (GetWorld() && GetWorld()->IsGameWorld()))
	{
		UE_LOG(LogChaosImpact, Verbose, TEXT("See-through walls on %s: %d blocks know their side"), *GetName(), Sided);
	}
}

void AChaosImpactStageBase::HideNearCamera(const TArray<FString>& Prefixes)
{
	NearCameraParts.Reset();
	TArray<UPrimitiveComponent*> Parts;
	GetComponents(Parts);
	for (UPrimitiveComponent* Part : Parts)
	{
		const FString PartName = Part->GetName();
		if (Prefixes.ContainsByPredicate([&PartName](const FString& Prefix) { return PartName.StartsWith(Prefix); }))
		{
			NearCameraParts.Add(Part);
		}
	}
}

// ---- Stage gimmick effects -----------------------------------------------------------------------------------------------

void AChaosImpactStageBase::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = FloorRings.Num() - 1; Index >= 0; --Index)
	{
		FFloorRing& Ring = FloorRings[Index];
		const float T = static_cast<float>((Now - Ring.StartedAt) / FMath::Max(Ring.Seconds, 0.05f));
		bool bAlive = T < 1.0f;
		for (const TWeakObjectPtr<UStaticMeshComponent>& Segment : Ring.Segments)
		{
			bAlive &= Segment.IsValid();
		}
		if (!bAlive)
		{
			for (const TWeakObjectPtr<UStaticMeshComponent>& Segment : Ring.Segments)
			{
				if (UStaticMeshComponent* Mesh = Segment.Get())
				{
					Mesh->DestroyComponent();
				}
			}
			FloorRings.RemoveAtSwap(Index);
			continue;
		}
		// Out fast then easing; in steady (it gathers to the moment).
		const float Ease = Ring.To >= Ring.From ? 1.0f - FMath::Pow(1.0f - T, 3.0f) : T * T;
		const float Radius = FMath::Lerp(Ring.From, Ring.To, Ease);
		const float Arc = 2.0f * PI * Radius / FloorRingSegments * 1.06f;
		for (int32 Piece = 0; Piece < Ring.Segments.Num(); ++Piece)
		{
			const float Angle = 2.0f * PI * Piece / Ring.Segments.Num();
			UStaticMeshComponent* Mesh = Ring.Segments[Piece].Get();
			Mesh->SetWorldLocationAndRotation(Ring.At + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f),
				FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f));
			Mesh->SetWorldScale3D(FVector(Ring.Thickness / 100.0f, Arc / 100.0f, 0.02f));
		}
		if (UMaterialInstanceDynamic* Look = Ring.Look.Get())
		{
			// A spreading ring fades as it goes; a gathering one brightens toward the end.
			const float Glow = Ring.To >= Ring.From ? FMath::Pow(1.0f - T, 1.6f) : 0.3f + 0.7f * T;
			Look->SetScalarParameterValue(TEXT("Intensity"), Ring.Intensity * Glow);
		}
	}
	for (int32 Index = TimedEffects.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* Effect = TimedEffects[Index].Effect.Get();
		if (!Effect || Now >= TimedEffects[Index].StopAt)
		{
			if (Effect)
			{
				Effect->Deactivate();
			}
			TimedEffects.RemoveAtSwap(Index);
		}
	}
}

void AChaosImpactStageBase::PlayFloorRing(const FVector& WorldAt, const FLinearColor& Color, const float FromRadius, const float ToRadius,
	const float Seconds, const float Thickness, const float Intensity)
{
	if (GetNetMode() == NM_DedicatedServer || !FxSegmentMesh || !GetWorld())
	{
		return;
	}
	// A flat band of light: segments round a circle, Thickness x 3 wide (so it reads from the camera's height).
	FFloorRing Ring;
	Ring.At = WorldAt + FVector(0.0f, 0.0f, 3.0f);
	Ring.StartedAt = FPlatformTime::Seconds();
	Ring.Seconds = Seconds;
	Ring.From = FromRadius;
	Ring.To = ToRadius;
	Ring.Thickness = Thickness * 3.0f;
	Ring.Intensity = Intensity;
	UMaterialInstanceDynamic* Look = FxGlowMaterial ? UMaterialInstanceDynamic::Create(FxGlowMaterial, this) : nullptr;
	if (Look)
	{
		Look->SetVectorParameterValue(TEXT("Color"), Color);
		Look->SetScalarParameterValue(TEXT("Intensity"), Intensity * (ToRadius >= FromRadius ? 1.0f : 0.3f));
	}
	Ring.Look = Look;
	for (int32 Piece = 0; Piece < FloorRingSegments; ++Piece)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Mesh->SetStaticMesh(FxSegmentMesh);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetupAttachment(GetRootComponent());
		Mesh->RegisterComponent();
		if (Look)
		{
			Mesh->SetMaterial(0, Look);
		}
		const float Angle = 2.0f * PI * Piece / FloorRingSegments;
		Mesh->SetWorldLocationAndRotation(Ring.At + FVector(FMath::Cos(Angle) * FromRadius, FMath::Sin(Angle) * FromRadius, 0.0f),
			FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f));
		Mesh->SetWorldScale3D(FVector(Ring.Thickness / 100.0f, 2.0f * PI * FromRadius / FloorRingSegments * 1.06f / 100.0f, 0.02f));
		Ring.Segments.Add(Mesh);
	}
	FloorRings.Add(Ring);
}

UNiagaraComponent* AChaosImpactStageBase::PlayStageEffect(const TCHAR* Path, const FVector& WorldAt, const FVector& Scale, const float Seconds,
	const FLinearColor* Color, const FRotator& Rotation)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return nullptr;
	}
	UNiagaraSystem* System = ChaosImpactBallTypes::LoadEffect(Path);
	UNiagaraComponent* Effect = System ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, WorldAt, Rotation, Scale) : nullptr;
	if (!Effect)
	{
		return nullptr;
	}
	if (Color)
	{
		ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Base Color"), *Color);
		ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Color"), *Color);
	}
	if (Seconds > 0.0f)
	{
		TimedEffects.Add({Effect, FPlatformTime::Seconds() + Seconds});
	}
	return Effect;
}

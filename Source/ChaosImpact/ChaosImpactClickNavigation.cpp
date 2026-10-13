#include "ChaosImpactClickNavigation.h"
#include "Algo/Reverse.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

bool ChaosImpactClickNavigation::FindPath(const ACharacter* Character, const FVector& GroundGoal, TArray<FVector>& OutPath)
{
	OutPath.Reset();
	if (!Character || !Character->GetWorld()) { return false; }
	UWorld* World = Character->GetWorld();
	const FVector Start = Character->GetActorLocation();
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + 4.0f;
	const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FCollisionObjectQueryParams Walls;
	Walls.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactMouseRoute), false, Character);
	const auto Floor = [&](const FVector& At, FVector& Out)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, At + FVector::UpVector * 350.0f,
			At - FVector::UpVector * 1600.0f, Walls, Query) || Hit.ImpactNormal.Z < 0.65f) { return false; }
		Out = Hit.ImpactPoint + FVector::UpVector * (Half + 4.0f);
		return !World->OverlapAnyTestByObjectType(Out, FQuat::Identity, Walls,
			FCollisionShape::MakeCapsule(Radius, Half - 2.0f), Query);
	};
	const auto Clear = [&](const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->SweepSingleByObjectType(Hit, From + FVector::UpVector * 4.0f, To + FVector::UpVector * 4.0f,
			FQuat::Identity, Walls, FCollisionShape::MakeCapsule(Radius, Half - 2.0f), Query);
	};
	FVector Goal;
	if (!Floor(GroundGoal, Goal) || FMath::Abs(Goal.Z - Start.Z) > 1200.0f) { return false; }
	if (FMath::Abs(Goal.Z - Start.Z) < 45.0f && Clear(Start, Goal))
	{
		// A clear horizontal sweep alone would cross an open pit; sample support as well.
		bool bSupported = true;
		const int32 Samples = FMath::CeilToInt(FVector::Dist2D(Start, Goal) / 100.0f);
		for (int32 I = 1; I < Samples && bSupported; ++I)
		{
			FVector Stand = FVector::ZeroVector;
			bSupported = Floor(FMath::Lerp(Start, Goal, static_cast<float>(I) / Samples), Stand)
				&& FMath::Abs(Stand.Z - Start.Z) < 45.0f;
		}
		if (bSupported) { OutPath.Add(Goal); return true; }
	}
	const FVector2D Min(FMath::Min(Start.X, Goal.X) - 1000, FMath::Min(Start.Y, Goal.Y) - 1000);
	const FVector2D Max(FMath::Max(Start.X, Goal.X) + 1000, FMath::Max(Start.Y, Goal.Y) + 1000);
	const double Cell = FMath::Max(100.0, FMath::Max(Max.X - Min.X, Max.Y - Min.Y) / 63.0);
	const int32 NX = FMath::CeilToInt((Max.X - Min.X) / Cell) + 1;
	const int32 NY = FMath::CeilToInt((Max.Y - Min.Y) / Cell) + 1;
	const int32 Count = NX * NY;
	TArray<FVector> Centers; Centers.SetNum(Count);
	TArray<int8> Free; Free.Init(-1, Count);
	TArray<float> Cost; Cost.Init(TNumericLimits<float>::Max(), Count);
	TArray<int32> Parent; Parent.Init(INDEX_NONE, Count);
	TArray<uint8> Closed; Closed.SetNumZeroed(Count);
	const auto IndexOf = [&](const FVector& At)
	{
		return FMath::Clamp(FMath::RoundToInt((At.Y - Min.Y) / Cell), 0, NY - 1) * NX
			+ FMath::Clamp(FMath::RoundToInt((At.X - Min.X) / Cell), 0, NX - 1);
	};
	const int32 First = IndexOf(Start), Last = IndexOf(Goal);
	const auto IsFree = [&](int32 Index)
	{
		if (Free[Index] < 0)
		{
			FVector Stand = FVector::ZeroVector;
			Free[Index] = Floor(FVector(Min.X + (Index % NX) * Cell, Min.Y + (Index / NX) * Cell, Start.Z), Stand) ? 1 : 0;
			Centers[Index] = Stand;
		}
		return Free[Index] == 1;
	};
	Centers[First] = Start; Free[First] = 1;
	Centers[Last] = Goal; Free[Last] = 1;
	TArray<int32> Open; Open.Add(First); Cost[First] = 0;
	static const FIntPoint Steps[] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
	for (int32 Visit = 0; Visit < Count && !Open.IsEmpty(); ++Visit)
	{
		int32 Best = 0;
		float Score = TNumericLimits<float>::Max();
		for (int32 I = 0; I < Open.Num(); ++I)
		{
			const int32 At = Open[I];
			const float Candidate = Cost[At] + FVector::Dist2D(Centers[At], Goal);
			if (Candidate < Score) { Best = I; Score = Candidate; }
		}
		const int32 Here = Open[Best]; Open.RemoveAtSwap(Best); Closed[Here] = 1;
		if (Here == Last)
		{
			for (int32 At = Last; At != First && At != INDEX_NONE; At = Parent[At]) { OutPath.Add(Centers[At]); }
			Algo::Reverse(OutPath);
			return !OutPath.IsEmpty();
		}
		for (const FIntPoint& Step : Steps)
		{
			const int32 X = Here % NX + Step.X, Y = Here / NX + Step.Y;
			if (X < 0 || Y < 0 || X >= NX || Y >= NY) { continue; }
			const int32 Next = Y * NX + X;
			if (Closed[Next] || !IsFree(Next) || FMath::Abs(Centers[Next].Z - Centers[Here].Z) > 140.0f) { continue; }
			if (Step.X && Step.Y && (!IsFree((Here / NX) * NX + X) || !IsFree(Y * NX + Here % NX))) { continue; }
			// Permit small ledges only if the raised capsule clears them; movement automatically jumps there.
			FVector From = Centers[Here], To = Centers[Next];
			From.Z = To.Z = FMath::Max(From.Z, To.Z) + 5;
			if (!Clear(From, To)) { continue; }
			const float NewCost = Cost[Here] + FVector::Dist2D(Centers[Here], Centers[Next]);
			if (NewCost < Cost[Next])
			{
				Cost[Next] = NewCost; Parent[Next] = Here; Open.AddUnique(Next);
			}
		}
	}
	return false;
}

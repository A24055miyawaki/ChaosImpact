#include "ChaosImpactDojoCourse.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
	const FLinearColor CourseWall(1.0f, 0.5f, 0.12f, 1.0f);
	const FLinearColor CourseWallTop(1.0f, 0.86f, 0.45f, 1.0f);
	const FLinearColor CourseLane(0.05f, 0.07f, 0.15f, 1.0f);

	UMaterialInstanceDynamic* FlatColour(UPrimitiveComponent* Component, const FLinearColor& Color)
	{
		static UMaterialInterface* Flat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
		if (!Flat || !Component)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Flat, Component);
		Material->SetVectorParameterValue(TEXT("Base Color"), Color);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Material);
		return Material;
	}

	/** The road's edge on one side: each corner pushed out along the bend's middle (a mitre), so the width holds. */
	TArray<FVector> OffsetLine(const TArray<FVector>& Path, const float Offset)
	{
		TArray<FVector> Out;
		for (int32 Index = 0; Index < Path.Num(); ++Index)
		{
			const FVector In = Index > 0 ? (Path[Index] - Path[Index - 1]).GetSafeNormal2D() : (Path[1] - Path[0]).GetSafeNormal2D();
			const FVector Next = Index + 1 < Path.Num() ? (Path[Index + 1] - Path[Index]).GetSafeNormal2D() : In;
			const FVector SideIn(-In.Y, In.X, 0.0f);
			const FVector SideNext(-Next.Y, Next.X, 0.0f);
			const FVector Mitre = (SideIn + SideNext).GetSafeNormal2D();
			const float Cos = FMath::Max(0.35f, static_cast<float>(FVector::DotProduct(Mitre, SideIn)));
			Out.Add(Path[Index] + Mitre * Offset / Cos);
		}
		return Out;
	}
}

AChaosImpactDojoCourse::AChaosImpactDojoCourse()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Lane = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Lane"));
	Lane->SetupAttachment(Root);
	Lane->SetCastShadow(false);
}

float AChaosImpactDojoCourse::PathLength(const TArray<FVector>& Path)
{
	float Length = 0.0f;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		Length += static_cast<float>(FVector::Dist2D(Path[Index - 1], Path[Index]));
	}
	return Length;
}

FVector AChaosImpactDojoCourse::PointAlong(const TArray<FVector>& Path, float Distance)
{
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const float Length = static_cast<float>(FVector::Dist2D(Path[Index - 1], Path[Index]));
		if (Distance <= Length)
		{
			return FMath::Lerp(Path[Index - 1], Path[Index], Length > 1.0f ? Distance / Length : 0.0f);
		}
		Distance -= Length;
	}
	return Path.IsEmpty() ? FVector::ZeroVector : Path.Last();
}

float AChaosImpactDojoCourse::DistanceToPath(const TArray<FVector>& Path, const FVector& Point, float& OutAlong)
{
	float Best = TNumericLimits<float>::Max();
	float Walked = 0.0f;
	OutAlong = 0.0f;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector A(Path[Index - 1].X, Path[Index - 1].Y, 0.0f);
		const FVector B(Path[Index].X, Path[Index].Y, 0.0f);
		const FVector P(Point.X, Point.Y, 0.0f);
		const float Length = static_cast<float>(FVector::Dist(A, B));
		const float T = Length > 1.0f ? FMath::Clamp(static_cast<float>(FVector::DotProduct(P - A, B - A)) / (Length * Length), 0.0f, 1.0f) : 0.0f;
		const float Distance = static_cast<float>(FVector::Dist(P, A + (B - A) * T));
		if (Distance < Best)
		{
			Best = Distance;
			OutAlong = Walked + Length * T;
		}
		Walked += Length;
	}
	return Best;
}

void AChaosImpactDojoCourse::AddWall(const FVector& From, const FVector& To, const float Z, const float BaseZ, const FLinearColor& Color)
{
	static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		return;
	}
	const FVector Along = (To - From).GetSafeNormal2D();
	// (A thickness past each end, so the pieces meet at the bends.)
	const float Length = static_cast<float>(FVector::Dist2D(From, To)) + WallThickness;
	// (From the floor below, up past the road by its height.)
	const float Bottom = FMath::Min(BaseZ, Z) - 2.0f;
	const float Tall = Z + WallHeight - Bottom;
	const FVector Middle((From.X + To.X) * 0.5, (From.Y + To.Y) * 0.5, Bottom + Tall * 0.5f);
	UStaticMeshComponent* Wall = NewObject<UStaticMeshComponent>(this);
	Wall->SetStaticMesh(Cube);
	Wall->SetupAttachment(Root);
	Wall->SetMobility(EComponentMobility::Movable);
	Wall->RegisterComponent();
	Wall->SetWorldLocationAndRotation(Middle, Along.Rotation());
	Wall->SetWorldScale3D(FVector(Length / 100.0f, WallThickness / 100.0f, Tall / 100.0f));
	Wall->SetCollisionProfileName(TEXT("BlockAll"));
	FlatColour(Wall, Color);
	Walls.Add(Wall);
	// A pale edge along its top, so each wall reads from above.
	UStaticMeshComponent* Top = NewObject<UStaticMeshComponent>(this);
	Top->SetStaticMesh(Cube);
	Top->SetupAttachment(Root);
	Top->SetMobility(EComponentMobility::Movable);
	Top->RegisterComponent();
	Top->SetWorldLocationAndRotation(FVector(Middle.X, Middle.Y, Z + WallHeight + 3.0f), Along.Rotation());
	Top->SetWorldScale3D(FVector(Length / 100.0f, (WallThickness + 6.0f) / 100.0f, 0.06f));
	Top->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Top->SetCastShadow(false);
	FlatColour(Top, CourseWallTop);
	Walls.Add(Top);
}

void AChaosImpactDojoCourse::Build(const TArray<FVector>& Path, const float HalfWidth, const float Mouth, const float BaseZ)
{
	if (Path.Num() < 2)
	{
		return;
	}
	// One level for the whole road (the highest floor under it: nothing of the stage stands up through it).
	const float Z = static_cast<float>(Path[0].Z);
	const TArray<FVector> Left = OffsetLine(Path, HalfWidth + WallThickness * 0.5f);
	const TArray<FVector> Right = OffsetLine(Path, -(HalfWidth + WallThickness * 0.5f));
	const TArray<FVector> LaneLeft = OffsetLine(Path, HalfWidth);
	const TArray<FVector> LaneRight = OffsetLine(Path, -HalfWidth);

	// The lane: a dark strip on the floor the width of the road.
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	for (int32 Index = 0; Index < Path.Num(); ++Index)
	{
		Vertices.Add(FVector(LaneLeft[Index].X, LaneLeft[Index].Y, Z));
		Vertices.Add(FVector(LaneRight[Index].X, LaneRight[Index].Y, Z));
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(0.0, Index));
		UVs.Add(FVector2D(1.0, Index));
		if (Index > 0)
		{
			const int32 A = (Index - 1) * 2;
			Triangles.Append({A, A + 2, A + 1, A + 1, A + 2, A + 3});
			Triangles.Append({A, A + 1, A + 2, A + 1, A + 3, A + 2});
		}
	}
	// (Walked on: the player starts on it, the CPU stands at its end.)
	Lane->bUseComplexAsSimpleCollision = true;
	Lane->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	Lane->SetCollisionProfileName(TEXT("BlockAll"));
	FlatColour(Lane, CourseLane);

	// The walls, from the mouth on (the player stands in the open before it).
	float Walked = 0.0f;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const float Length = static_cast<float>(FVector::Dist2D(Path[Index - 1], Path[Index]));
		const float From = FMath::Clamp((Mouth - Walked) / FMath::Max(Length, 1.0f), 0.0f, 1.0f);
		Walked += Length;
		if (From >= 1.0f)
		{
			continue;
		}
		AddWall(FMath::Lerp(Left[Index - 1], Left[Index], From), Left[Index], Z, BaseZ, CourseWall);
		AddWall(FMath::Lerp(Right[Index - 1], Right[Index], From), Right[Index], Z, BaseZ, CourseWall);
	}
}

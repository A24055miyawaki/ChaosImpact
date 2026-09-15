#include "ChaosImpactIceMeshes.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"

namespace
{
	using ChaosImpactIceMeshes::FMeshBuffers;

	/**
	 * Adds one flat-shaded triangle facing away from Inside. The procedural mesh treats triangles as front
	 * faces when (C - A) x (B - A) points toward the viewer, so corners are emitted as A, C, B.
	 */
	void AddFacet(FMeshBuffers& Mesh, const FTransform& Transform, const FVector& Inside,
		FVector A, FVector B, FVector C)
	{
		FVector Normal = FVector::CrossProduct(B - A, C - A);
		if (Normal.SizeSquared() < UE_KINDA_SMALL_NUMBER)
		{
			return;
		}
		if (FVector::DotProduct(Normal, (A + B + C) / 3.0f - Inside) < 0.0f)
		{
			Swap(B, C);
			Normal = -Normal;
		}
		const FVector WorldNormal = Transform.TransformVectorNoScale(Normal.GetSafeNormal());
		for (const FVector& Corner : {A, C, B})
		{
			const FVector Placed = Transform.TransformPosition(Corner);
			Mesh.Triangles.Add(Mesh.Vertices.Add(Placed));
			Mesh.Normals.Add(WorldNormal);
			Mesh.UVs.Add(FVector2D(Corner.X, Corner.Y) * 0.01f);
		}
	}

	TArray<FVector> MakeRing(const int32 Sides, const float Radius, const float Z, const float RadiusJitter,
		const float AngleJitter, FRandomStream& Stream)
	{
		TArray<FVector> Ring;
		Ring.Reserve(Sides);
		const float Offset = Stream.FRand() * UE_TWO_PI;
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const float Angle = Offset + (Side + Stream.FRandRange(-AngleJitter, AngleJitter)) * UE_TWO_PI / Sides;
			const float R = Radius * Stream.FRandRange(1.0f - RadiusJitter, 1.0f + RadiusJitter);
			Ring.Add(FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, Z));
		}
		return Ring;
	}

	void BridgeRings(FMeshBuffers& Mesh, const FTransform& Transform, const FVector& Inside,
		const TArray<FVector>& Lower, const TArray<FVector>& Upper)
	{
		const int32 Sides = Lower.Num();
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const int32 Next = (Side + 1) % Sides;
			AddFacet(Mesh, Transform, Inside, Lower[Side], Lower[Next], Upper[Side]);
			AddFacet(Mesh, Transform, Inside, Lower[Next], Upper[Next], Upper[Side]);
		}
	}

	void CapRing(FMeshBuffers& Mesh, const FTransform& Transform, const FVector& Inside,
		const TArray<FVector>& Ring, const FVector& Apex)
	{
		for (int32 Side = 0; Side < Ring.Num(); ++Side)
		{
			AddFacet(Mesh, Transform, Inside, Ring[Side], Ring[(Side + 1) % Ring.Num()], Apex);
		}
	}
}

void ChaosImpactIceMeshes::AppendCrystal(FMeshBuffers& Mesh, const FTransform& Transform, const float Radius,
	const float Height, FRandomStream& Stream)
{
	constexpr int32 Sides = 6;
	const FVector Inside(0.0f, 0.0f, Height * 0.4f);
	// Sunk slightly into the ground, widest just below the shoulder, then a faceted point.
	const TArray<FVector> Base = MakeRing(Sides, Radius * 0.9f, -Radius * 0.3f, 0.12f, 0.12f, Stream);
	TArray<FVector> Shoulder;
	for (const FVector& Corner : Base)
	{
		Shoulder.Add(FVector(Corner.X * Stream.FRandRange(0.95f, 1.12f), Corner.Y * Stream.FRandRange(0.95f, 1.12f),
			Height * Stream.FRandRange(0.66f, 0.8f)));
	}
	const FVector Tip(Stream.FRandRange(-0.15f, 0.15f) * Radius, Stream.FRandRange(-0.15f, 0.15f) * Radius, Height);
	BridgeRings(Mesh, Transform, Inside, Base, Shoulder);
	CapRing(Mesh, Transform, Inside, Shoulder, Tip);
	CapRing(Mesh, Transform, Inside, Base, FVector(0.0f, 0.0f, -Radius * 0.3f));
}

void ChaosImpactIceMeshes::AppendChunk(FMeshBuffers& Mesh, const FTransform& Transform, const float Radius,
	const float HalfHeight, FRandomStream& Stream)
{
	constexpr int32 Sides = 8;
	const FVector Inside = FVector::ZeroVector;
	TArray<TArray<FVector>> Rings;
	const float Levels[] = {-1.0f, -0.45f, 0.2f, 0.78f};
	const float Widths[] = {1.02f, 1.1f, 1.04f, 0.8f};
	for (int32 Level = 0; Level < UE_ARRAY_COUNT(Levels); ++Level)
	{
		Rings.Add(MakeRing(Sides, Radius * Widths[Level], HalfHeight * Levels[Level] + Stream.FRandRange(-6.0f, 6.0f),
			0.14f, 0.18f, Stream));
	}
	for (int32 Level = 0; Level + 1 < Rings.Num(); ++Level)
	{
		BridgeRings(Mesh, Transform, Inside, Rings[Level], Rings[Level + 1]);
	}
	CapRing(Mesh, Transform, Inside, Rings.Last(),
		FVector(Stream.FRandRange(-0.2f, 0.2f) * Radius, Stream.FRandRange(-0.2f, 0.2f) * Radius,
			HalfHeight * Stream.FRandRange(1.0f, 1.12f)));
	CapRing(Mesh, Transform, Inside, Rings[0], FVector(0.0f, 0.0f, -HalfHeight));
}

void ChaosImpactIceMeshes::AppendGem(FMeshBuffers& Mesh, const FTransform& Transform, const float Radius,
	FRandomStream& Stream)
{
	// Icosahedron with every corner pushed in or out, so each facet sits at its own angle.
	const float T = (1.0f + FMath::Sqrt(5.0f)) * 0.5f;
	TArray<FVector> Corners = {
		{-1, T, 0}, {1, T, 0}, {-1, -T, 0}, {1, -T, 0},
		{0, -1, T}, {0, 1, T}, {0, -1, -T}, {0, 1, -T},
		{T, 0, -1}, {T, 0, 1}, {-T, 0, -1}, {-T, 0, 1}};
	for (FVector& Corner : Corners)
	{
		Corner = Corner.GetSafeNormal() * Radius * Stream.FRandRange(0.82f, 1.14f);
	}
	constexpr int32 Faces[20][3] = {
		{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
		{1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
		{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
		{4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
	for (const auto& Face : Faces)
	{
		AddFacet(Mesh, Transform, FVector::ZeroVector, Corners[Face[0]], Corners[Face[1]], Corners[Face[2]]);
	}
}

void ChaosImpactIceMeshes::AppendSheet(FMeshBuffers& Mesh, const FTransform& Transform, const float Radius,
	FRandomStream& Stream)
{
	constexpr int32 Sides = 30;
	// Facets face up (away from a point below the sheet); tiny height changes make them catch light.
	const FVector Inside(0.0f, 0.0f, -20.0f);
	TArray<FVector> Edge = MakeRing(Sides, Radius, 0.0f, 0.0f, 0.3f, Stream);
	for (FVector& Point : Edge)
	{
		// A ragged outline: most points wander a little, some cut deep notches.
		const float Pull = Stream.FRand() < 0.2f ? Stream.FRandRange(0.68f, 0.84f) : Stream.FRandRange(0.92f, 1.06f);
		Point.X *= Pull;
		Point.Y *= Pull;
	}
	TArray<FVector> Middle;
	for (const FVector& Point : Edge)
	{
		Middle.Add(FVector(Point.X * Stream.FRandRange(0.45f, 0.6f), Point.Y * Stream.FRandRange(0.45f, 0.6f),
			Stream.FRandRange(0.0f, 1.2f)));
	}
	BridgeRings(Mesh, Transform, Inside, Edge, Middle);
	CapRing(Mesh, Transform, Inside, Middle, FVector(0.0f, 0.0f, 0.8f));
}

void ChaosImpactIceMeshes::AppendCrack(FMeshBuffers& Mesh, const FTransform& Transform, const FVector& Start,
	const FVector& Direction, const float Length, const float Width, FRandomStream& Stream)
{
	constexpr int32 Segments = 6;
	FVector Point = Start;
	FVector Heading = Direction.GetSafeNormal2D();
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const FVector Next = Point + Heading * (Length / Segments);
		const float Taper = 1.0f - 0.75f * Segment / static_cast<float>(Segments);
		const FVector Side = FVector::CrossProduct(FVector::UpVector, (Next - Point).GetSafeNormal()) * Width * Taper * 0.5f;
		const FVector Below = (Point + Next) * 0.5f - FVector::UpVector * 20.0f;
		AddFacet(Mesh, Transform, Below, Point - Side, Point + Side, Next + Side * 0.8f);
		AddFacet(Mesh, Transform, Below, Point - Side, Next + Side * 0.8f, Next - Side * 0.8f);
		Heading = Heading.RotateAngleAxis(Stream.FRandRange(-28.0f, 28.0f), FVector::UpVector);
		Point = Next;
	}
}

UProceduralMeshComponent* ChaosImpactIceMeshes::CreateComponent(AActor* Owner, USceneComponent* Parent,
	const FMeshBuffers& Mesh, UMaterialInterface* Material)
{
	if (!Owner || Mesh.Vertices.IsEmpty())
	{
		return nullptr;
	}
	UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(Owner);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCastShadow(false);
	Component->bUseComplexAsSimpleCollision = false;
	Component->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
	Component->CreateMeshSection(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs,
		TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Component->SetMaterial(0, Material);
	Component->RegisterComponent();
	return Component;
}

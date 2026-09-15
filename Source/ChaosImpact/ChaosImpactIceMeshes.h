#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;

/** Faceted ice geometry built at runtime, so crystals catch light on sharp flat faces. */
namespace ChaosImpactIceMeshes
{
	struct FMeshBuffers
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
	};

	/** A six-sided crystal standing on the local +Z axis from the origin, with a pointed tip. */
	void AppendCrystal(FMeshBuffers& Mesh, const FTransform& Transform, float Radius, float Height, FRandomStream& Stream);

	/** A rough block of ice centered on the origin, HalfHeight along Z. */
	void AppendChunk(FMeshBuffers& Mesh, const FTransform& Transform, float Radius, float HalfHeight, FRandomStream& Stream);

	/** A nearly flat sheet of ice on the local XY plane with a ragged, notched outline. */
	void AppendSheet(FMeshBuffers& Mesh, const FTransform& Transform, float Radius, FRandomStream& Stream);

	/** A thin, zig-zagging crack line lying on the local XY plane, narrowing toward its end. */
	void AppendCrack(FMeshBuffers& Mesh, const FTransform& Transform, const FVector& Start, const FVector& Direction,
		float Length, float Width, FRandomStream& Stream);

	/** A jagged, roughly round lump of ice centered on the origin. */
	void AppendGem(FMeshBuffers& Mesh, const FTransform& Transform, float Radius, FRandomStream& Stream);

	/** Registers a render-only procedural mesh component holding Mesh. */
	UProceduralMeshComponent* CreateComponent(AActor* Owner, USceneComponent* Parent, const FMeshBuffers& Mesh,
		UMaterialInterface* Material);
}

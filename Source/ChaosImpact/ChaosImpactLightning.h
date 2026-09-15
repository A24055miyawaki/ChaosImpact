#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactIceMeshes.h"

class AActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UWorld;

/** Jagged lightning drawn as flat ribbons, rebuilt every few frames so it flickers. */
namespace ChaosImpactLightning
{
	/**
	 * A zig-zagging, forking bolt from Start to End, narrowing toward its end. The ribbon's flat side is turned
	 * against Facing: the camera's view direction for bolts in the air, up for arcs lying on the ground.
	 */
	void AppendBolt(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Start, const FVector& End, float Width,
		const FVector& Facing, FRandomStream& Stream, int32 Forks = 2);

	/** A thin flat ring lying on the horizontal plane, for shock waves and range edges. */
	void AppendRing(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Center, float Radius, float Width,
		int32 Segments = 48);

	/** A render-only procedural mesh component for bolts, empty until SetMesh. */
	UProceduralMeshComponent* CreateComponent(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material);

	/** Replaces the component's bolts. */
	void SetMesh(UProceduralMeshComponent* Component, const ChaosImpactIceMeshes::FMeshBuffers& Mesh);

	/** Where this machine's first player camera looks. */
	FVector GetViewDirection(const UWorld* World);
}

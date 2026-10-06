#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactDriveBall.generated.h"

class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * A drive ball's look, built on the ball (AChaosImpactBall keeps it): a white-hot core in a golden glow, two energy rings
 * spinning round it, a golden trail that draws every turn it is steered through, and, while it is being steered, a ring
 * round it that runs down with the time left.
 */
struct FChaosImpactDriveLook
{
	struct FTrailPoint
	{
		FVector Position = FVector::ZeroVector;
		float Time = 0.0f;
	};
	TWeakObjectPtr<USceneComponent> Root;
	TWeakObjectPtr<UStaticMeshComponent> Core;
	TWeakObjectPtr<UStaticMeshComponent> Halo;
	TWeakObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
	TWeakObjectPtr<UProceduralMeshComponent> Rings;
	TWeakObjectPtr<UProceduralMeshComponent> Trail;
	TWeakObjectPtr<UPointLightComponent> Light;
	TWeakObjectPtr<UNiagaraComponent> Sparks;
	TArray<FTrailPoint> TrailPoints;
	/** Its last flat heading, to see it snap round. */
	FVector LastHeading = FVector::ZeroVector;
	bool bShown = true;
	bool IsBuilt() const { return Root.IsValid(); }
};

namespace ChaosImpactDrive
{
	CHAOSIMPACT_API void BuildLook(AActor* Owner, USceneComponent* Parent, FChaosImpactDriveLook& Out);
	/**
	 * ControlLeft: the share (0-1) of its steering time left, or negative while nobody steers it. Velocity: to show a snap
	 * round with a burst of sparks.
	 */
	CHAOSIMPACT_API void UpdateLook(AActor* Owner, FChaosImpactDriveLook& Look, float Time, float DeltaSeconds, bool bFlying,
		bool bVisible, float ControlLeft, const FVector& Velocity);
}

/** Render-only burst of a drive ball: an explosion where it hit someone, or a fizzle of sparks when its time ran out. */
UCLASS(NotPlaceable, Transient)
class AChaosImpactDriveBurst final : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactDriveBurst();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	static void Play(UWorld* World, const FVector& Location, bool bHit);

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> BurstRoot;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> BurstLight;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> ShockRing;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;

	bool bHit = false;
	float Age = 0.0f;
};

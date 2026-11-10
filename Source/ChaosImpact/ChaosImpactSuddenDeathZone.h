#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactSuddenDeathZone.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

/**
 * A duel's (or a tournament arena's) sudden death: the floor still in play, a circle round the arena's middle that
 * closes in. It starts out past the corners, sweeps in to the square's sides in its first seconds, then shrinks to
 * nothing by the bell. Its edge is a thin glowing line on the floor with a narrow red band just outside it
 * (the field stays in view). The host judges who is outside (ChaosImpactGameModeSuddenDeath.cpp); every machine draws
 * it from the replicated start, on the room's shared clock.
 */
UCLASS(NotPlaceable)
class AChaosImpactSuddenDeathZone : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSuddenDeathZone();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** The arena's half width; the circle starts past its corners, reaches its sides after SweepSeconds, nothing at the end. */
	void Begin(float InHalfExtent, double InStartAt, float InTotalSeconds);
	/** The circle's radius now (on the room's clock). */
	float GetRadius() const;
	bool IsInside(const FVector& Location) const;

	/** Before it starts closing in, and how long its first sweep (corners to sides) takes. */
	static constexpr float DelaySeconds = 1.5f;
	static constexpr float SweepSeconds = 5.0f;
	static constexpr float CornerReach = 1.42f;
	/** Seconds outside the circle before it counts (the HUD's gauge fills in this time). */
	static constexpr float OutSeconds = 1.0f;

private:
	UPROPERTY(Replicated)
	float HalfExtent = 1000.0f;

	UPROPERTY(Replicated)
	double StartAt = 0.0;

	UPROPERTY(Replicated)
	float TotalSeconds = 30.0f;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Edge;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Outside;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EdgeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OutsideMaterial;

	double ServerNow() const;
};

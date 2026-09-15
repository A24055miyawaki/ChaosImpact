#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactHazardZone.generated.h"

class AChaosImpactCharacter;
class APawn;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * What a special ball leaves behind. The server spawns it, applies the blast or freeze once and runs the
 * burn damage; every machine builds the same effect locally from the replicated type and seed.
 */
UCLASS(NotBlueprintable)
class AChaosImpactHazardZone : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactHazardZone();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: detonates a special ball at Location. DirectVictim already took the ball's own hit. */
	static AChaosImpactHazardZone* Detonate(UWorld* World, EChaosImpactBallType Type, const FVector& Location,
		APawn* SourcePawn, AActor* DirectVictim);
	/** Any machine: true on active frozen ground. Each machine applies the slide to the characters it moves. */
	static bool IsSlipperyAt(const UWorld* World, const FVector& FeetLocation);
	/** Server: a remote player's screen saw the ball hit them just before it detonated elsewhere here. */
	bool TryApplyLateHit(AChaosImpactCharacter* Victim);

	EChaosImpactBallType GetZoneType() const { return ZoneType; }
	float GetRadius() const;
	float GetActiveSeconds() const;

	static constexpr float FireRadius = 240.0f;
	static constexpr float FireBurnSeconds = 4.0f;
	static constexpr float FireBurnInterval = 1.0f;
	static constexpr float IceRadius = 260.0f;
	static constexpr float IceFloorSeconds = 5.0f;
	static constexpr float IceFreezeSeconds = 2.0f;
	static constexpr float FadeSeconds = 0.6f;
	/** Flames and mist are allowed to die out after the zone stops. */
	static constexpr float EffectTailSeconds = 2.0f;
	static constexpr float ZoneDamage = 1.0f;

protected:
	virtual void BeginPlay() override;

	void ApplyDetonationEffects();
	void TickBurning();
	bool IsInside(const AActor* Actor, float Padding, float MaxHeight) const;
	AController* GetSourceController() const;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBurnHit(FVector_NetQuantize Location);

	UPROPERTY(Replicated)
	EChaosImpactBallType ZoneType = EChaosImpactBallType::Fire;

	/** Height of the impact above the ground the zone sits on, where the blast is drawn. */
	UPROPERTY(Replicated)
	float BurstHeight = 40.0f;

	/** The thrower is never hurt or frozen by their own ball. */
	UPROPERTY(Replicated)
	TObjectPtr<APawn> SourcePawn;

	UPROPERTY(Replicated)
	int32 VisualSeed = 0;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> ZoneLight;

	TWeakObjectPtr<AActor> DirectVictim;
	TSet<TWeakObjectPtr<AActor>> Affected;
	TMap<TWeakObjectPtr<AActor>, double> NextBurnAt;
	double SpawnedAt = 0.0;

	// Presentation: Niagara Examples Pack systems plus runtime ice geometry.
	struct FCrystal
	{
		TWeakObjectPtr<USceneComponent> Component;
		float Delay = 0.0f;
	};
	TArray<FCrystal> Crystals;
	/** Frozen-ground layers that spread out from the impact. */
	TArray<TWeakObjectPtr<USceneComponent>> IceSheets;
	/** Materials faded out with the zone, each with its full opacity. */
	TArray<TPair<TWeakObjectPtr<UMaterialInstanceDynamic>, float>> FadingMaterials;
	TArray<TWeakObjectPtr<UNiagaraComponent>> LoopingEffects;
	TWeakObjectPtr<UNiagaraComponent> ImpactMist;
	bool bPresentationBuilt = false;
	bool bLoopingStopped = false;
	bool bMistStopped = false;
	bool bThawPlayed = false;

	void BuildPresentation();
	void BuildFirePresentation(FRandomStream& Stream);
	void BuildIcePresentation(FRandomStream& Stream);
	UNiagaraComponent* AddLoopingEffect(const TCHAR* SystemPath, const FVector& RelativeLocation);
	void UpdatePresentation(float Age);
};

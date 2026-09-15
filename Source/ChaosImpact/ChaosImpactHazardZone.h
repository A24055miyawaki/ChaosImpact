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
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMeshComponent;

/** One runtime effect shape (engine sphere or cylinder) with its glow material, and spare values for animating it. */
struct FChaosImpactZoneMesh
{
	TWeakObjectPtr<UStaticMeshComponent> Mesh;
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	float Angle = 0.0f;
	float Radius = 0.0f;
	float Height = 0.0f;
	float Seed = 1.0f;
};

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
	/**
	 * Any machine: how far open black holes draw this character this frame. The thrower and their teammates
	 * are never pulled. Applied where the character is moved, like the slide on ice.
	 */
	static FVector GetBlackHolePullOffset(const UWorld* World, AActor* Character, float DeltaSeconds);
	/** Server: a remote player's screen saw the ball hit them just before it detonated elsewhere here. */
	bool TryApplyLateHit(AChaosImpactCharacter* Victim);

	EChaosImpactBallType GetZoneType() const { return ZoneType; }
	APawn* GetSourcePawn() const { return SourcePawn; }
	/** True while a repeating burn tick is being applied, so it is not scored as a new hit. */
	bool IsApplyingBurnTick() const { return bApplyingBurnTick; }
	float GetRadius() const;
	float GetActiveSeconds() const;

	static constexpr float FireRadius = 240.0f;
	static constexpr float FireBurnSeconds = 4.0f;
	static constexpr float FireBurnInterval = 1.0f;
	static constexpr float IceRadius = 260.0f;
	static constexpr float IceFloorSeconds = 5.0f;
	static constexpr float IceFreezeSeconds = 2.0f;
	static constexpr float ThunderRadius = 320.0f;
	/** The lightning itself; the damage lands once, at the moment of the burst. */
	static constexpr float ThunderActiveSeconds = 0.6f;
	/** Reach of a black hole's pull. */
	static constexpr float BlackHoleRadius = 760.0f;
	static constexpr float BlackHoleSeconds = 4.0f;
	/**
	 * Pull speed at the edge of the reach and near the centre. Both outrun a walking player, so only dashing
	 * gets out: no pull during a dash and for BlackHoleDashGraceSeconds after, a few dashes' worth of stamina.
	 */
	static constexpr float BlackHolePullSpeed = 800.0f;
	static constexpr float BlackHoleCoreSpeed = 700.0f;
	static constexpr float BlackHoleDashGraceSeconds = 0.3f;
	static constexpr float BlackHoleCoreHeight = 120.0f;
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

	/** Electricity crawling over someone the lightning struck, on every machine. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShock(AActor* Victim);

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
	bool bApplyingBurnTick = false;

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

	// Thunder: a strike from the sky, arcs racing over the ground, a flash and a shock ring.
	void BuildThunderPresentation();
	void RebuildThunderBolts(int32 Flicker);
	void UpdateThunderPresentation(float Age);
	// Black hole: a lightless core, spinning rings, rings and motes drawn inward, tethers to whoever is pulled.
	void BuildBlackHolePresentation(FRandomStream& Stream);
	void UpdateBlackHolePresentation(float Age);
	void UpdateBlackHoleTethers(const FVector& Core, float Strength);
	UStaticMeshComponent* AddZoneMesh(FChaosImpactZoneMesh& Out, bool bSphere, UMaterialInstanceDynamic* Material);

	FChaosImpactZoneMesh FlashSphere;
	/** Thin rings (shock wave, black hole reach and inflow), rebuilt each frame. */
	TWeakObjectPtr<UProceduralMeshComponent> RingMesh;
	TWeakObjectPtr<UMaterialInstanceDynamic> RingMaterial;
	FChaosImpactZoneMesh GroundGlow;
	FChaosImpactZoneMesh CoreSphere;
	FChaosImpactZoneMesh HorizonSphere;
	FChaosImpactZoneMesh HaloSphere;
	TArray<FChaosImpactZoneMesh> DiskRings;
	TArray<FChaosImpactZoneMesh> Motes;
	TMap<TWeakObjectPtr<AActor>, FChaosImpactZoneMesh> Tethers;
	TWeakObjectPtr<UProceduralMeshComponent> StrikeMesh;
	TWeakObjectPtr<UProceduralMeshComponent> ArcMesh;
	TWeakObjectPtr<UMaterialInstanceDynamic> StrikeMaterial;
	TWeakObjectPtr<UMaterialInstanceDynamic> ArcMaterial;
	int32 FlickerIndex = 0;
	float NextFlickerAge = 0.0f;
	float LastPresentationAge = 0.0f;
	bool bCollapseBurstPlayed = false;
};

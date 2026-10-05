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

	/**
	 * Server: detonates a special ball at Location. DirectVictim already took the ball's own hit. Scale sizes the
	 * burst's look (a snowball's size); gameplay reach does not change with it.
	 */
	static AChaosImpactHazardZone* Detonate(UWorld* World, EChaosImpactBallType Type, const FVector& Location,
		APawn* SourcePawn, AActor* DirectVictim, float Scale = 1.0f);
	/**
	 * Server: a small fire on the ground under a flying fire ball (its trail). No blast; it burns like the fire
	 * zone. Leader is the first fire of the same trail, which keeps one burn clock for all of them so standing
	 * where two touch does not burn twice. Returns nullptr over a drop with no ground near.
	 */
	static AChaosImpactHazardZone* SpawnFireTrail(UWorld* World, const FVector& BallLocation, APawn* SourcePawn,
		AChaosImpactHazardZone* Leader);
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
	/** One small fire of a fire ball's trail (SpawnFireTrail). */
	bool IsFireTrail() const { return bFireTrail; }
	APawn* GetSourcePawn() const { return SourcePawn; }
	float GetRadius() const;
	float GetActiveSeconds() const;
	/** Seconds since it burst. */
	float GetAge() const;

	static constexpr float FireRadius = 240.0f;
	static constexpr float FireBurnSeconds = 4.0f;
	static constexpr float FireBurnInterval = 1.0f;
	/** One fire of a fire ball's trail. */
	static constexpr float FireTrailRadius = 95.0f;
	static constexpr float FireTrailSeconds = 3.0f;
	static constexpr float IceRadius = 380.0f;
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
	/** The very centre of a black hole burns like fire (FireBurnInterval) whoever it has drawn in. */
	static constexpr float BlackHoleBurnRadius = 140.0f;
	/** A beam fading out at the end of its range, and a snowball bursting: only a moment to show. */
	static constexpr float BeamFadeSeconds = 0.35f;
	static constexpr float SnowBurstSeconds = 1.1f;
	/** Nova: how long its dome of light stands before it thins away. */
	static constexpr float NovaBlastSeconds = 1.6f;
	static constexpr float FadeSeconds = 0.6f;
	/** Flames and mist are allowed to die out after the zone stops. */
	static constexpr float EffectTailSeconds = 2.0f;
	static constexpr float ZoneDamage = 1.0f;

protected:
	virtual void BeginPlay() override;

	void ApplyDetonationEffects();
	/** Server: burns whoever stands within Radius, once every FireBurnInterval each. */
	void TickBurning(float Radius);
	/** Radius below zero means the zone's own (GetRadius). */
	bool IsInside(const AActor* Actor, float Padding, float MaxHeight, float Radius = -1.0f) const;
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

	/** One small fire of a fire ball's trail rather than where a ball burst. */
	UPROPERTY(Replicated)
	bool bFireTrail = false;

	/** How big the burst looks (Detonate's Scale). */
	UPROPERTY(Replicated)
	float BurstScale = 1.0f;

	/** Server, smoke: blinds opponents inside the cloud while it hangs. */
	void TickSmoke();

	// Smoke: a thick cloud of plumes; beam: a flash of pink light; snow: chunks of snow flying apart.
	void BuildSmokePresentation(FRandomStream& Stream);
	void BuildBeamPresentation();
	void BuildSnowPresentation(FRandomStream& Stream);
	void UpdateSnowChunks(float Age);
	/**
	 * Nova: a blinding flash, a dome of light racing out to the blast's edge, a pillar of light, shock waves along the
	 * ground and explosions going off all over inside; the camera shakes for players nearby.
	 */
	void BuildNovaPresentation(FRandomStream& Stream);
	/** Server: blows someone caught in a nova outward, off their feet. */
	void ApplyNovaKnockback(AChaosImpactCharacter* Victim) const;
	void UpdateNovaPresentation(float Age);
	struct FNovaBurst
	{
		FVector Location = FVector::ZeroVector;
		float At = 0.0f;
		float Scale = 1.0f;
		bool bPlayed = false;
	};
	TArray<FNovaBurst> NovaBursts;
	struct FSnowChunk
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FVector Start = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FRotator Spin = FRotator::ZeroRotator;
		float Size = 1.0f;
	};
	TArray<FSnowChunk> SnowChunks;

	/** Server: the trail's first fire, whose NextBurnAt the whole trail shares. */
	TWeakObjectPtr<AChaosImpactHazardZone> TrailLeader;

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
	void BuildFireTrailPresentation(FRandomStream& Stream);
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

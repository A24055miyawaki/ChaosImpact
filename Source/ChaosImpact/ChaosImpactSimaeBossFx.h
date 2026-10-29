#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactSimaeBossFx.generated.h"

class AChaosImpactBall;
class AChaosImpactFxKit;
class AChaosImpactSimaeBoss;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * The shima-enaga boss's effects and attacks (solo stage 8): ground warnings, shock blasts, the 剛球 (a plain ball
 * driven straight through a wide lane) and the snowballs of its avalanche. All of them are made on the machine that
 * runs the boss (solo mode is played offline), none replicates.
 */
namespace ChaosImpactSimaeFx
{
	/** Snow powder: kicked-up dust coloured like the snow (pale, faintly blue in the shade). */
	inline const FLinearColor SnowDust(0.84f, 0.9f, 1.0f, 1.0f);
	/** A puff of powder snow rising and drifting (a smoke plume, stopped after Seconds so it thins out). */
	void PlaySnowDust(UWorld* World, const FVector& Location, float Size, float Seconds = 0.35f, float Rate = 60.0f);
	/** Snow thrown up in a burst: clods, powder and glittering crystals. */
	void PlaySnowBurst(UWorld* World, const FVector& Location, float Scale);
	/** The floor under a point (or the point itself when there is none). */
	FVector FindGround(const UWorld* World, const FVector& Point, const AActor* Ignore = nullptr);

	/**
	 * Online (the RPG sets NetHook; empty everywhere else): a host's warnings, blasts and flames, sent to its guests'
	 * screens. Kind: 0 circle, 1 lane, 2 blast, 3 flames, 4 a lane re-aimed, 5 a warning gone off.
	 */
	struct FNetFx
	{
		uint8 Kind = 0;
		int32 Id = 0;
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		float F0 = 0.0f, F1 = 0.0f, F2 = 0.0f;
		int32 Count = 0;
		bool bFlag = false;
		FLinearColor Color = FLinearColor::Red;
	};
	extern TFunction<void(UWorld*, const FNetFx&)> NetHook;
}

/** A warning laid on the ground: a circle that fills up, or a lane with arrows running along it, until it goes off. */
UCLASS(NotBlueprintable, Transient)
class AChaosImpactSimaeWarning : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeWarning();
	virtual void Tick(float DeltaSeconds) override;

	static AChaosImpactSimaeWarning* SpawnCircle(UWorld* World, const FVector& Center, float Radius, float Seconds,
		const FLinearColor& Color);
	static AChaosImpactSimaeWarning* SpawnLane(UWorld* World, const FVector& Start, const FVector& Direction, float Length,
		float HalfWidth, float Seconds, const FLinearColor& Color);
	/** A lane following its aim (still aiming). */
	void SetLane(const FVector& Start, const FVector& Direction, float Length);
	/** Off now: flashes and fades. */
	void Finish();
	/** Online: which of the host's warnings this is (0 offline). */
	int32 NetFxId = 0;

private:
	void Rebuild();
	/** A lane stops at the first wall in its way (it is not drawn running under the scenery). */
	float TrimToWalls(const FVector& Start, float WantedLength) const;

	/**
	 * The whole area on one soft plane (M_CIFX_Warn): see-through red, deeper toward a soft bright rim, filling as it
	 * comes due (full: it goes off); a lane has chevrons running along it. No hard edge anywhere.
	 */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Plane;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Look;

	bool bLane = false;
	float Radius = 300.0f;
	float Length = 1000.0f;
	float HalfWidth = 150.0f;
	FVector Direction = FVector::ForwardVector;
	float Seconds = 1.0f;
	float Age = 0.0f;
	float FinishedAge = -1.0f;
	FLinearColor Color = FLinearColor::Red;
};

/** A blast on the ground (cosmetic): soft shock rings racing out, a flash, cracks, clods of snow and powder flung about. */
UCLASS(NotBlueprintable, Transient)
class AChaosImpactSimaeBlast : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeBlast();
	virtual void Tick(float DeltaSeconds) override;

	/** Radius: how far the rings run. Heat: 0 a white shock of snow, 1 the full-power red. */
	static AChaosImpactSimaeBlast* Play(UWorld* World, const FVector& Center, float Radius, float Heat, bool bCracks = true,
		int32 Clods = 14);

private:
	void Rebuild();

	/** The shock itself (rings, flash, powder) is soft FX-kit pieces; the cracks and clods are kept here. */
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Cracks;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HotMaterial;
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Flash;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ClodMeshes;

	struct FClod
	{
		FVector Velocity;
		FVector Spin;
		float Size = 1.0f;
	};
	TArray<FClod> Clods;
	float Radius = 500.0f;
	float Heat = 0.0f;
	float Age = 0.0f;
	int32 Seed = 0;
};

/**
 * 剛球: a plain ball, driven dead straight through a wide lane in a gale, ploughing up the snow. Whoever is in the lane
 * is hit and blown away; it stops at a wall or at the end of its range and is then an ordinary ball lying there.
 */
UCLASS(NotBlueprintable, Transient)
class AChaosImpactSimaeGoukyu : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeGoukyu();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static AChaosImpactSimaeGoukyu* Launch(AChaosImpactSimaeBoss* Boss, AChaosImpactBall* Ball, const FVector& Start,
		const FVector& Direction, float Speed, float HitRadius, float Range, float Knockback, float Heat);
	bool IsFlying() const { return bFlying; }

private:
	void Stop(const FVector& Where);

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Glow;
	UPROPERTY()
	TObjectPtr<UAudioComponent> Roar;
	/** Its light streak, the sparks it sheds and (full power) its trail of fire. */
	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> Trails;
	/**
	 * Soft FX-kit pieces: Around rides with the ball (its glow, the air streaming off it); Wake stays where it passed
	 * (the shock rings it throws off over the snow, the ploughed track).
	 */
	TWeakObjectPtr<AChaosImpactFxKit> Around;
	TWeakObjectPtr<AChaosImpactFxKit> Wake;
	FLinearColor WindColor = FLinearColor::White;
	float NextTrackAt = 0.0f;

	TWeakObjectPtr<AChaosImpactSimaeBoss> Boss;
	TWeakObjectPtr<AChaosImpactBall> Ball;
	/** Where along its path the next shock ring bursts off it. */
	float NextBoomAt = 120.0f;
	float NextShakeAt = 0.0f;
	TArray<TWeakObjectPtr<AActor>> AlreadyHit;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float Speed = 2600.0f;
	float HitRadius = 200.0f;
	float Range = 4000.0f;
	float Knockback = 1400.0f;
	float Heat = 0.0f;
	float Travelled = 0.0f;
	float StartHeight = 200.0f;
	float GroundZ = 0.0f;
	float NextDustAt = 0.0f;
	float Age = 0.0f;
	float StoppedAge = -1.0f;
	bool bFlying = false;
};

/** One snowball of the avalanche: its warning on the ground, then it drops out of the sky and bursts. */
UCLASS(NotBlueprintable, Transient)
class AChaosImpactSimaeBoulder : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeBoulder();
	virtual void Tick(float DeltaSeconds) override;

	static AChaosImpactSimaeBoulder* Drop(AChaosImpactSimaeBoss* Boss, const FVector& Ground, float Size, float HitRadius,
		float WarningSeconds, float Knockback, float Heat);

private:
	void Land();

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ball;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shadow;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShadowMaterial;

	TWeakObjectPtr<AChaosImpactSimaeBoss> Boss;
	TWeakObjectPtr<AChaosImpactSimaeWarning> Warning;
	FVector Ground = FVector::ZeroVector;
	float Size = 120.0f;
	float HitRadius = 260.0f;
	float WarningSeconds = 1.0f;
	float Knockback = 1300.0f;
	float Heat = 0.0f;
	float Age = 0.0f;
	bool bLanded = false;
	static constexpr float FallSeconds = 0.42f;
	static constexpr float FallHeight = 2600.0f;
};

/**
 * The power-up's eruption of fire (cosmetic): a column of flame bursting up round the body, and blobs of fire flung out
 * in every direction, streaming flame behind them, that land and burn on the snow for a moment.
 */
UCLASS(NotBlueprintable, Transient)
class AChaosImpactSimaeFlameBurst : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeFlameBurst();
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Ground: under the body. Height: how tall the body is (the column and where the blobs leave from scale with it).
	 * Without bColumn only the blobs fly (smaller, slower: sputters while the power wells up).
	 */
	static AChaosImpactSimaeFlameBurst* Play(UWorld* World, const FVector& Ground, float Height, int32 Blobs = 26, bool bColumn = true);

private:
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Flash;

	struct FBlob
	{
		FVector Location;
		FVector Velocity;
		float Size = 100.0f;
		float Seed = 0.0f;
		float Life = 1.2f;
		bool bLanded = false;
		/** Real fire riding on it (it streams out behind as the blob flies), left burning where it lands. */
		TWeakObjectPtr<UNiagaraComponent> Fire;
	};
	TArray<FBlob> Blobs;
	FVector Ground = FVector::ZeroVector;
	float Height = 400.0f;
	float Age = 0.0f;
	bool bColumn = true;
};

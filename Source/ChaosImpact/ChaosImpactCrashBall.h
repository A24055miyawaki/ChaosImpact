#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "ChaosImpactCrashBall.generated.h"

class AChaosImpactBall;
class AChaosImpactCrashBall;
class AChaosImpactStageBase;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Network snapshot replicated from server to clients for smooth Giant Ball rendering. */
class AChaosImpactCrashGoal;

USTRUCT()
struct FChaosImpactCrashBallNetState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize10 Velocity = FVector::ZeroVector;

	UPROPERTY()
	FRotator RollRotation = FRotator::ZeroRotator;

	UPROPERTY()
	int8 LastHitTeam = -1;

	UPROPERTY()
	uint8 bLastHitCharged : 1;

	UPROPERTY()
	uint8 bRecovering : 1;

	UPROPERTY()
	uint16 HitRevision = 0;

	UPROPERTY()
	uint16 RecoveryRevision = 0;

	/** How many hits in a row the team that pushed last has made (1 = a lone hit): the bigger, the bigger the ring. */
	UPROPERTY()
	uint8 Combo = 0;

	/** Frozen by an ice ball: it cannot move or be pushed until then (server time). */
	UPROPERTY()
	float FrozenUntil = 0.0f;

	FChaosImpactCrashBallNetState()
		: bLastHitCharged(false)
		, bRecovering(false)
	{
	}
};

/**
 * A team's goal in クラッシュボール: a round pedestal in its corner of the stage (the two goals stand at opposite
 * corners). The Giant Ball pushed near it is drawn in (the nearer, the harder: the defenders must knock it back out in
 * time); on top of it, in its middle, it is a knockout. Team colour rim, a target in the middle, four marker pylons,
 * the team's crest floating over it.
 */
UCLASS()
class AChaosImpactCrashGoal : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactCrashGoal();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Sets which team owns/defends this goal (0 = Red, 1 = Blue). */
	void SetTeamIndex(int32 InTeamIndex);
	int32 GetTeamIndex() const { return TeamIndex; }

	/** Returns the team that scores when the Giant Ball enters this goal (the opposing team). */
	int32 GetScoringTeamIndex() const { return TeamIndex == 0 ? 1 : 0; }

	/** World location of the center of the goal mouth opening on the floor. */
	FVector GetGoalMouthLocation() const { return GetActorLocation(); }

	/** True when the Giant Ball has been drawn onto the middle of the pedestal (a knockout). */
	bool IsBallScored(const AChaosImpactCrashBall* Ball) const;

	/** The pedestal's radius, how far out it draws the Giant Ball in, and how near its middle the ball scores. */
	static constexpr float PedestalRadius = 330.0f;
	static constexpr float PullRadius = 950.0f;
	static constexpr float ScoreRadius = 150.0f;
	/** How hard it draws the ball in at the edge of its pull and right by it (cm/s^2). */
	static constexpr float PullAtEdge = 520.0f;
	static constexpr float PullAtCentre = 1500.0f;

	/** Triggers the goal explosion flash when a KO victory is scored in this goal. */
	UFUNCTION(NetMulticast, Reliable)
	void TriggerGoalScoredEffect();

	/** Team index that defends this goal (0 = Red, 1 = Blue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_TeamIndex, Category="Chaos Impact|Crash Ball")
	int32 TeamIndex = 0;

	/** Clear interior width of the goal mouth opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_TeamIndex, Category="Chaos Impact|Crash Ball", meta=(ClampMin="320.0", ClampMax="1200.0"))
	float OpeningWidth = 560.0f;

	/** Clear interior height of the goal mouth opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_TeamIndex, Category="Chaos Impact|Crash Ball", meta=(ClampMin="240.0", ClampMax="800.0"))
	float OpeningHeight = 360.0f;

	/** Interior depth of the goal pocket before the solid backstop wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_TeamIndex, Category="Chaos Impact|Crash Ball", meta=(ClampMin="160.0", ClampMax="600.0"))
	float GoalDepth = 260.0f;

protected:
	UFUNCTION()
	void OnRep_TeamIndex();

	void RebuildGoalGeometry();
	void ApplyTeamVisuals(float PulseAlpha = 0.0f);
	UStaticMeshComponent* CreateGoalPart(const FName& Name, UStaticMesh* Mesh, bool bCollision);

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> ThresholdPad;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> GoalLineBeam;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> LeftPost;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> RightPost;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> LeftFunnelBumper;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> RightFunnelBumper;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> Crossbar;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> BackWall;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> LeftSideWall;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> RightSideWall;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> RoofGuard;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> EnergyNet;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> CrestCenter;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> CrestWingLeft;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> CrestWingRight;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UBoxComponent> GoalTriggerBox;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UPointLightComponent> GoalLight;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlatMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	double ScoredFlashAt = -100.0;
	mutable bool bApproachObserved = false;
};

/**
 * The Giant Ball (クラッシュボール) at the heart of Crash Ball mode.
 * Pushed by thrown balls and special ball effects, climbs slopes/ramps smoothly, decelerates naturally,
 * and automatically recovers to its last verified safe position if knocked out of bounds or wedged.
 */
UCLASS()
class AChaosImpactCrashBall : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactCrashBall();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Resets the Giant Ball to its starting center floor position and clears velocity/history. */
	void ResetToStartLocation(const FVector& FloorLocation, AChaosImpactStageBase* InStage);

	/** Applies a push from a thrown ball impact on the server. Returns true if the hit was accepted. */
	bool ApplyBallHit(AChaosImpactBall* IncomingBall, const FHitResult& Hit);

	/**
	 * Applies a capped push from a special ball hazard zone, tornado, beam, or summon.
	 * Uses per-source cooldowns so continuous/multi-hit effects cannot launch the Giant Ball across the map.
	 */
	bool ApplySpecialEffectPush(AActor* SourceActor, APawn* InstigatorPawn, const FVector& WorldOrigin,
		const FVector& PreferredDirection, float StrengthScale = 1.0f, bool bIsContinuousZone = false);

	/** Immediately recovers the Giant Ball to its last safe ground position (or initial center). */
	void RecoverToSafeLocation();

	float GetBallRadius() const { return BallRadius; }
	FVector GetBallVelocity() const { return Velocity; }
	int32 GetLastHitTeamIndex() const { return LastHitTeamIndex; }
	double GetLastHitServerTime() const { return LastHitServerTime; }
	double GetLastTeamHitServerTime(int32 TeamIndex) const;
	bool IsRecovering() const { return RecoveryFreezeRemaining > 0.0f || NetState.bRecovering; }

	/**
	 * How hard TeamIndex's pushes land where the ball is now: harder deep in the team's own half (defending its
	 * goal), lighter deep in the opponents' half, so a lead is never one volley from a knockout.
	 */
	float GetTerritoryPushScale(int32 TeamIndex) const;

	/** Radius of the Giant Ball sphere in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="80.0", ClampMax="260.0"))
	float BallRadius = 135.0f;

	/** Maximum horizontal speed of the Giant Ball in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="400.0", ClampMax="3000.0"))
	float MaxSpeed = 1450.0f;

	/** Ground rolling deceleration in cm/s^2 so the ball stops instead of rolling forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="100.0", ClampMax="2000.0"))
	float RollingDeceleration = 520.0f;

	/** Air horizontal drag deceleration in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="20.0", ClampMax="1000.0"))
	float AirDeceleration = 260.0f;

	/** Downward gravity acceleration in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="400.0", ClampMax="4000.0"))
	float GravityAcceleration = 1850.0f;

	/** Maximum ledge/step height the Giant Ball can smoothly roll up without snagging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="15.0", ClampMax="110.0"))
	float MaxStepUpHeight = 64.0f;

	/** Minimum surface normal Z considered a walkable floor/ramp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.45", ClampMax="0.90"))
	float WalkableFloorZ = 0.62f;

	/** Wall bounce restitution coefficient when the Giant Ball hits a vertical wall or barrier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.05", ClampMax="0.85"))
	float WallBounceRestitution = 0.36f;

	/** Push speed added by an uncharged throw (ChargeAlpha = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="80.0", ClampMax="1200.0"))
	float MinThrowPushSpeed = 330.0f;

	/** Push speed added by a fully charged throw (ChargeAlpha = 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="200.0", ClampMax="1800.0"))
	float MaxChargeThrowPushSpeed = 820.0f;

	/** Base push speed added by a direct special ball detonation or beam hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="100.0", ClampMax="1200.0"))
	float SpecialDirectPushSpeed = 360.0f;

	/** Base push speed added per pulse by a continuous hazard zone, tornado, or summon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="60.0", ClampMax="800.0"))
	float SpecialZonePushSpeed = 250.0f;

	/** Push multiplier for a team with the ball right in front of its own goal (defending). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="1.0", ClampMax="2.0"))
	float DeepDefencePushScale = 1.2f;

	/** Push multiplier for a team with the ball right in front of the opponents' goal (attacking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.4", ClampMax="1.0"))
	float DeepAttackPushScale = 0.88f;

	/** A team's hits on it within this many seconds of each other build a combo, each pushing harder (up to ComboMaxScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.3", ClampMax="4.0"))
	float ComboWindowSeconds = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.0", ClampMax="0.6"))
	float ComboStepScale = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="1.0", ClampMax="2.5"))
	float ComboMaxScale = 1.6f;

	/** How long an ice ball freezes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.0", ClampMax="5.0"))
	float IceFreezeSeconds = 2.0f;

	/** Rolling at least this fast into an opponent of the team that pushed it last, it knocks them flying. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="100.0", ClampMax="1500.0"))
	float KnockMinSpeed = 380.0f;

	/** Frozen by an ice ball now. */
	bool IsFrozen() const;
	/** The goals that draw it in when it comes near (set when the match is set up). */
	void SetPullGoals(const TArray<AChaosImpactCrashGoal*>& Goals);
	/** The combo the team that pushed last is on (1 = a lone hit; 0 = none yet). */
	int32 GetCombo() const { return NetState.Combo; }

	/** Cooldown in seconds before the same thrown ball actor can push the Giant Ball again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.08", ClampMax="2.0"))
	float PerBallHitCooldown = 0.30f;

	/** Cooldown in seconds between pushes from the same special effect zone/tornado/summon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.20", ClampMax="3.0"))
	float SpecialSourceCooldown = 0.60f;

	/** Interval in seconds between safe floor position checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.05", ClampMax="2.0"))
	float SafeRecordInterval = 0.25f;

	/** Seconds of repeated blocked pushes with almost no movement before stuck recovery triggers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.8", ClampMax="8.0"))
	float StuckTimeThreshold = 2.0f;

	/** Minimum distance the Giant Ball must move while being pushed to avoid stuck recovery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball", meta=(ClampMin="15.0", ClampMax="150.0"))
	float StuckMoveThreshold = 42.0f;

protected:
	UFUNCTION()
	void OnRep_NetState();

	void StepServerPhysics(float DeltaSeconds);
	void StepSingleSubstep(float SubDt);
	void ResolveCharacterOverlaps();
	void UpdateSafePositionAndRecovery(float DeltaSeconds);
	bool IsLocationSafeOnFloor(const FVector& CandidateCenter, FVector& OutSnappedCenter) const;
	void RecordPushImpulse(const FVector& HorizontalDeltaVelocity, int32 TeamIndex, bool bCharged);
	void UpdateVisuals(float DeltaSeconds);
	void ShakeNearbyPlayers() const;
	double GetServerNow() const;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> CoreSphereMesh;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> EquatorialBandMesh;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> MeridianBandAMesh;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> MeridianBandBMesh;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> DirectionRingMesh;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UStaticMeshComponent> ShockwaveRingMesh;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Crash Ball")
	TObjectPtr<UPointLightComponent> CoreLight;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlatMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(ReplicatedUsing=OnRep_NetState)
	FChaosImpactCrashBallNetState NetState;

	UPROPERTY()
	TWeakObjectPtr<AChaosImpactStageBase> CachedStage;

	FVector Velocity = FVector::ZeroVector;
	FRotator RollRotation = FRotator::ZeroRotator;
	FVector InitialSpawnLocation = FVector(0.0f, 0.0f, 135.0f);
	FVector LastSafeLocation = FVector(0.0f, 0.0f, 135.0f);
	bool bHasSafeLocation = false;
	bool bGrounded = true;

	int32 LastHitTeamIndex = INDEX_NONE;
	int32 ComboCount = 0;
	TArray<TWeakObjectPtr<AChaosImpactCrashGoal>> PullGoals;
	TMap<FObjectKey, double> LastKnockBySource;
	double LastHitServerTime = -100.0;
	double LastTeamHitServerTime[2] = {-100.0, -100.0};

	TMap<FObjectKey, double> LastHitTimeBySource;
	double LastSpecialZonePushTime = -100.0;

	float SafeCheckAccumulator = 0.0f;
	float RecoveryFreezeRemaining = 0.0f;
	FVector StuckAnchorLocation = FVector::ZeroVector;
	float StuckAccumulatedTime = 0.0f;
	int32 StuckPushCount = 0;

	// Client presentation state
	double ClientHitFlashAt = -100.0;
	bool bClientLastHitCharged = false;
	double ClientRecoveryFlashAt = -100.0;
	uint16 ClientSeenHitRevision = 0;
	uint16 ClientSeenRecoveryRevision = 0;
};

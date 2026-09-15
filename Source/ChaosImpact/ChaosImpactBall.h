// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactBall.generated.h"

class AChaosImpactHazardZone;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UProceduralMeshComponent;
class UPointLightComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class USceneComponent;
class AChaosImpactCharacter;
class APawn;

UENUM(BlueprintType)
enum class EChaosImpactBallFlightMode : uint8
{
	Straight UMETA(DisplayName="Straight"),
	Arc UMETA(DisplayName="Arc")
};

/** Compact ball motion sent to clients, which predict and smooth locally between updates. */
USTRUCT()
struct FChaosImpactBallNetState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Location;

	UPROPERTY()
	FVector_NetQuantize10 Velocity;

	/** Server world time at which Location/Velocity were sampled. */
	UPROPERTY()
	double ServerTime = 0.0;

	/** EChaosImpactBallNetMode. */
	UPROPERTY()
	uint8 Mode = 0;
};

/** A damage-dealing dodgeball that rebounds from blocking geometry. */
UCLASS(Blueprintable)
class AChaosImpactBall : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactBall();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Launches with either the original level flight or a gravity-driven arc. */
	void Launch(const FVector& Direction, float Speed,
		EChaosImpactBallFlightMode FlightMode = EChaosImpactBallFlightMode::Arc,
		float ArcUpwardSpeed = 650.0f);

	/** Holds the real projectile on a hand socket until the animation release cue. */
	void PrepareForAnimatedThrow(USceneComponent* HandParent, FName HandSocket,
		const FVector& RelativeLocation, const FRotator& RelativeRotation);

	/** Turns this actor into a stationary ball that players can collect. */
	void MakePickup();
	void MakeRollingPickup(const FVector& ImpactVelocity);

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	bool IsPickup() const { return bIsPickup; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	bool IsPickupAvailable() const;

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	bool HasReflected() const { return bHasReflected; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	int32 GetReflectionCount() const { return ReflectionCount; }

	bool WasThrownBy(const APawn* Pawn) const;
	APawn* GetThrowingPawn() const { return ThrowingPawn.Get(); }
	FVector GetBallVelocity() const;
	/** Server: a remote player's client saw this ball hit them. Returns true if it was plausible and applied. */
	bool AcceptReportedHit(AChaosImpactCharacter* Victim, const FVector& HitLocation);
	/** Server: gives this pickup to a player (overlap here, or a claim from their own screen). */
	bool ConsumePickup(AChaosImpactCharacter* Collector);
	/** Client: the server refused this screen's pickup claim; show the ball again. */
	void CancelLocalPickupClaim();
	/** Set before BeginPlay on a client-only throw preview: no damage, pickups or replication. */
	void SetCosmeticPrediction(bool bCosmetic) { bCosmeticPrediction = bCosmetic; }
	float GetDamage() const { return Damage; }
	/** Set before FinishSpawning: what this ball does when it lands. */
	void SetBallType(EChaosImpactBallType Type);
	EChaosImpactBallType GetBallType() const { return BallType; }
	bool IsSpecialBall() const { return BallType != EChaosImpactBallType::Normal; }
	bool HasDetonated() const { return bDetonated; }
	/** Tuning and tests: how long a landed ball lies around, and how much of that time it spends blinking. */
	void SetLandedPickupLifetime(const float Seconds, const float BlinkSeconds)
	{
		LandedPickupLifetimeSeconds = FMath::Max(Seconds, 0.1f);
		LandedPickupBlinkSeconds = FMath::Max(BlinkSeconds, 0.0f);
	}

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	UFUNCTION()
	void HandlePickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);
	void DropToGroundAsPickup();
	/** Damage, contact flash and drop, shared by server collisions and client-reported hits. */
	void ResolveDamagingHit(AActor* OtherActor, const FVector& ImpactPoint, const FVector& ImpactNormal);
	/** Client: checks the displayed ball path against this machine's own player. */
	void TryReportLocalHit(const FVector& From, const FVector& To);
	/** Client: grabs a pickup the local player touches on this screen. */
	void TryClaimLocalPickup();
	/** Client: the thrower's own screen continues from its cosmetic preview instead of a second ball. */
	void TryAdoptPredictedThrow(const FVector& Desired);
	void EndCosmeticFlight();
	/** Server: a special ball bursts into its hazard zone instead of becoming a pickup. */
	void Detonate(const FVector& Location, AActor* DirectVictim);
	UFUNCTION()
	void OnRep_BallType();
	UFUNCTION()
	void OnRep_Detonated();
	/** Fire/ice look: glowing core, aura, crystals, light and a trail while flying. */
	void ApplyBallTypePresentation();
	void UpdateBallTypePresentation(float DeltaSeconds);

	/**
	 * Online, visual only (moves BallMesh, never the actor): a ball reaching another player's drawn body
	 * waits there for the hit to be confirmed, and an unconfirmed ball grazes past instead of through them.
	 */
	void UpdateContactPresentation(float DeltaSeconds);
	bool IsFlyingForPresentation() const;
	bool ShouldHoldForCharacter(const AChaosImpactCharacter* Character) const;
	/** Client: carries an in-progress hold over from this screen's throw preview. */
	void InheritContactPresentation(const AChaosImpactBall& Preview);
	void ResetContactPresentation();
	/** Development (-CIBallTrace): logs where this screen draws its own player's thrown balls each frame. */
	void TraceLocalThrow() const;

	/** Contact flash on every machine; gameplay resolution itself stays on the server. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastContactBurst(FVector_NetQuantize Location, FRotator Rotation);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.0"))
	float Damage = 1.0f;

	/** Scale of the short contact burst. This plays on every damaging hit, not only a KO. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball|Effects", meta=(ClampMin="0.1"))
	float ContactEffectScale = 0.85f;

	/** Flight timeout. The ball becomes a pickup instead of being destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.1"))
	float LifeSeconds = 8.0f;

	/**
	 * A thrown ball that has landed and rolls away disappears after this many seconds unless someone picks
	 * it up. Balls placed by spawners or summoned in training hover instead and never expire.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.1"))
	float LandedPickupLifetimeSeconds = 10.0f;

	/** The final part of that time, shown by blinking faster and faster before the ball shrinks away. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.0"))
	float LandedPickupBlinkSeconds = 3.0f;

	/** Server time at which this landed ball is removed; 0 while flying, held, or a spawner pickup. */
	UPROPERTY(Replicated)
	double LandedPickupExpiresAt = 0.0;

	/** Every machine blinks and shrinks the ball from the replicated expiry time. */
	void UpdateExpiryPresentation(float DeltaSeconds);
	FVector BallMeshBaseScale = FVector(0.48f);
	float ExpiryBlinkPhase = 0.0f;
	float ExpiryScale = 1.0f;
	bool bExpiryShown = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Bounciness = 1.0f;

	/** Prevents the pawn that was just hit from instantly collecting the ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.0"))
	float HitPickupLockoutSeconds = 0.55f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	bool bHasReflected = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	int32 ReflectionCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	EChaosImpactBallFlightMode ActiveFlightMode = EChaosImpactBallFlightMode::Arc;

	UPROPERTY(VisibleInstanceOnly, Replicated, BlueprintReadOnly, Category="Chaos Impact|Ball")
	bool bIsPickup = false;

	UPROPERTY(VisibleInstanceOnly, Replicated, BlueprintReadOnly, Category="Chaos Impact|Ball")
	bool bIsRolling = false;

	UPROPERTY(ReplicatedUsing=OnRep_NetState)
	FChaosImpactBallNetState NetState;

	UFUNCTION()
	void OnRep_NetState();
	/** Server: publishes the current motion for clients (no-op offline). */
	void UpdateNetState();
	/** Client: where the ball should be now, extrapolated from the last server state. */
	FVector PredictNetLocation(double ServerNow) const;
	void TickClientPresentation(float DeltaSeconds);
	double GetServerNow() const;
	/**
	 * Client: a steady estimate of the server time whose state is just arriving. Built from the ball
	 * states themselves (lowest observed delay), so it advances smoothly instead of in 0.1 s steps.
	 */
	double GetPresentationServerTime() const;
	void ObserveServerTime(double ServerTime);

	/** Client: display offset that decays to zero so corrections never snap. */
	FVector ClientErrorOffset = FVector::ZeroVector;
	bool bClientHasPresentation = false;
	float ClientHoverTime = 0.0f;

	/** Lets clients skip their own throws when judging hits. */
	UPROPERTY(Replicated)
	TObjectPtr<APawn> ReplicatedThrower;

	/** Client: this ball already hit the local player; hold it at the contact point until the server resolves. */
	bool bClientHitReported = false;
	double ClientHitFrozenUntil = 0.0;

	bool bCosmeticPrediction = false;
	/** Client: a pickup claim is waiting for the server (the ball is hidden meanwhile). */
	bool bClientPickupClaimed = false;
	double ClientPickupRetryAt = 0.0;
	double ClientRollingSince = 0.0;
	/** Client: this ball's throw may still take over the thrower's preview (set for new balls and on each new hold). */
	bool bAwaitingLaunchAdoption = true;
	/** Client: the server time the displayed position was last computed for. */
	double ClientLastPresentationTime = 0.0;
	int32 AdoptionWaitFrames = 0;
	/** Client: correction speed; slower while continuing from this screen's own throw preview. */
	float ActiveErrorDecayRate = 14.0f;
	/**
	 * Client, the thrower's own ball only: how far ahead of the arriving server state it is shown, kept from
	 * the throw preview so the ball does not appear to slow down when the server's copy takes over.
	 */
	float ClientTimeLead = 0.0f;
	static constexpr float MaxOwnThrowTimeLeadSeconds = 0.18f;
	/** Server: when this ball last stopped flying, so hits reported a moment later still count. */
	double FlightEndedAt = 0.0;

	/** Mesh offset from the actor used by UpdateContactPresentation. */
	FVector VisualOffset = FVector::ZeroVector;
	FVector LastContactCheckLocation = FVector::ZeroVector;
	bool bHasContactCheckLocation = false;
	bool bContactHolding = false;
	bool bContactHoldUsed = false;
	FVector ContactHoldPoint = FVector::ZeroVector;
	double ContactHoldUntil = 0.0;
	TWeakObjectPtr<AChaosImpactCharacter> ContactTarget;
	double ContactHoldStartedAt = 0.0;
	/** After a short still moment, an unconfirmed contact is drawn as the likely hit: dropped and rolling. */
	bool bContactDropping = false;
	FVector ContactDropStart = FVector::ZeroVector;
	FVector ContactDropVelocity = FVector::ZeroVector;
	double ContactDropStartedAt = 0.0;

	UPROPERTY(ReplicatedUsing=OnRep_BallType)
	EChaosImpactBallType BallType = EChaosImpactBallType::Normal;

	UPROPERTY(ReplicatedUsing=OnRep_Detonated)
	bool bDetonated = false;

	/** Server: the burst is kept briefly so a hit reported from a remote screen just before still counts. */
	double DetonatedAt = 0.0;
	TWeakObjectPtr<AChaosImpactHazardZone> DetonationZone;

	/** Ice: faceted crystal lump around the frosted core. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TypeVisual;

	/** Fire: flames that stay on the ball. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> TypeAuraEffect;

	/** Trail shown only while the ball flies, for every ball type. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> FlightTrailEffect;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> TypeLight;

	/** Thunder: crackling shell; black: violet event horizon. Ignores the ball's own scale and spin. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TypeGlow;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TypeGlowMaterial;

	/** Thunder: arcs of lightning jumping off the ball, rebuilt every few frames. */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> TypeArcs;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TypeArcMaterial;

	/** Black: tilted rings spinning around the ball. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> TypeRings;

	float NextArcFlickerTime = 0.0f;

	bool bFlightTrailOn = false;
	bool bTypePresentationBuilt = false;
	float TypeFxTime = 0.0f;

	FVector PickupBaseLocation = FVector::ZeroVector;
	float PickupAnimationTime = 0.0f;
	float FlightSeconds = 0.0f;
	float PickupAvailableAtSeconds = 0.0f;
	bool bPickupConsumed = false;
	TWeakObjectPtr<APawn> ThrowingPawn;
};

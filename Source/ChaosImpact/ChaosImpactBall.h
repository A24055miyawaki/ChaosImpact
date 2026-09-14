// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBall.generated.h"

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

	/** Client: display offset that decays to zero so corrections never snap. */
	FVector ClientErrorOffset = FVector::ZeroVector;
	bool bClientHasPresentation = false;
	float ClientHoverTime = 0.0f;

	FVector PickupBaseLocation = FVector::ZeroVector;
	float PickupAnimationTime = 0.0f;
	float FlightSeconds = 0.0f;
	float PickupAvailableAtSeconds = 0.0f;
	bool bPickupConsumed = false;
	TWeakObjectPtr<APawn> ThrowingPawn;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBall.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class AChaosImpactCharacter;
class APawn;

UENUM(BlueprintType)
enum class EChaosImpactBallFlightMode : uint8
{
	Straight UMETA(DisplayName="Straight"),
	Arc UMETA(DisplayName="Arc")
};

/** A damage-dealing dodgeball that rebounds from blocking geometry. */
UCLASS(Blueprintable)
class AChaosImpactBall : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactBall();
	virtual void Tick(float DeltaSeconds) override;

	/** Launches with either the original level flight or a gravity-driven arc. */
	void Launch(const FVector& Direction, float Speed,
		EChaosImpactBallFlightMode FlightMode = EChaosImpactBallFlightMode::Straight,
		float ArcUpwardSpeed = 650.0f);

	/** Turns this actor into a stationary ball that players can collect. */
	void MakePickup();

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	bool IsPickup() const { return bIsPickup; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	bool HasReflected() const { return bHasReflected; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball")
	int32 GetReflectionCount() const { return ReflectionCount; }

	bool WasThrownBy(const APawn* Pawn) const;
	APawn* GetThrowingPawn() const { return ThrowingPawn.Get(); }

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.0"))
	float Damage = 1.0f;

	/** Flight timeout. The ball becomes a pickup instead of being destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.1"))
	float LifeSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Bounciness = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	bool bHasReflected = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	int32 ReflectionCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	EChaosImpactBallFlightMode ActiveFlightMode = EChaosImpactBallFlightMode::Straight;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	bool bIsPickup = false;

	FVector PickupBaseLocation = FVector::ZeroVector;
	float PickupAnimationTime = 0.0f;
	float FlightSeconds = 0.0f;
	bool bPickupConsumed = false;
	TWeakObjectPtr<APawn> ThrowingPawn;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactTrainingTarget.generated.h"

class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EChaosImpactTargetMotion : uint8
{
	Stationary UMETA(DisplayName="Stationary"),
	SideToSide UMETA(DisplayName="Side To Side"),
	ForwardBack UMETA(DisplayName="Forward / Back")
};

/** A reusable training dummy that falls, bursts, disappears, and quickly respawns. */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Training Target"))
class AChaosImpactTrainingTarget : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactTrainingTarget();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	void ConfigureMotion(EChaosImpactTargetMotion NewMotion, float NewTravelDistance = 260.0f,
		float NewCyclesPerSecond = 0.32f, float StartPhase = 0.0f);
	/** Shows/hides this authored target immediately without destroying its editor placement. */
	void SetTrainingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Training Target")
	bool IsDefeated() const { return bDefeated; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Training Target")
	bool IsRespawning() const { return bRespawning; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Training Target")
	EChaosImpactTargetMotion GetMotionMode() const { return MotionMode; }

protected:
	virtual void BeginPlay() override;

private:
	void Defeat(const FVector& ImpactPoint);
	void RespawnTarget();
	void SetTargetVisible(bool bVisible);
	void ApplyTrainingEnabled();

	/** Plays the fall/burst on the server and every client; each side then respawns locally. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastDefeat(FVector_NetQuantize ImpactPoint);

	UFUNCTION()
	void OnRep_TrainingEnabled();

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> FallingAssembly;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> PoleMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BagMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> FacePlateMesh;

	UPROPERTY(VisibleAnywhere, Category="Effects")
	TObjectPtr<UStaticMeshComponent> ShockCore;

	UPROPERTY(VisibleAnywhere, Category="Effects")
	TArray<TObjectPtr<UStaticMeshComponent>> BurstPieces;

	UPROPERTY(VisibleAnywhere, Category="Effects")
	TObjectPtr<UPointLightComponent> HitLight;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Target")
	EChaosImpactTargetMotion MotionMode = EChaosImpactTargetMotion::Stationary;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Target", meta=(ClampMin="0.0"))
	float TravelDistance = 260.0f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Target", meta=(ClampMin="0.05"))
	float CyclesPerSecond = 0.32f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Target", meta=(ClampMin="0.5"))
	float RespawnDelay = 2.6f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Target", meta=(ClampMin="0.1"))
	float FallDuration = 0.58f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Target", meta=(ClampMin="0.15"))
	float RespawnGrowDuration = 0.55f;

	FVector HomeLocation = FVector::ZeroVector;
	float MotionTime = 0.0f;
	float MotionPhase = 0.0f;
	float DefeatTime = 0.0f;
	float RespawnTime = 0.0f;
	FVector ImpactLocalLocation = FVector(0.0f, 0.0f, 132.0f);
	bool bDefeated = false;
	bool bRespawning = false;
	UPROPERTY(ReplicatedUsing=OnRep_TrainingEnabled)
	bool bTrainingEnabled = true;
	FTimerHandle RespawnTimer;
};

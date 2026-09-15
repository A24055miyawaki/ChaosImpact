#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactBallSpawner.generated.h"

class AChaosImpactBall;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** Training pickup point that replaces its ball after a short delay. */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Ball Spawn Point"))
class AChaosImpactBallSpawner : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactBallSpawner();
	AChaosImpactBall* GetActiveBall() const { return ActiveBall.Get(); }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void TrySpawnBall();

	/** Mostly normal balls; a special one now and then. */
	EChaosImpactBallType RollBallType() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> SpawnPad;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UPointLightComponent> SpawnLight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chaos Impact|Training")
	TSubclassOf<AChaosImpactBall> BallClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.25"))
	float RespawnInterval = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training")
	float BallHeight = 38.0f;

	/** Chance (0-1) that a new ball is a fire ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FireBallChance = 0.15f;

	/** Chance (0-1) that a new ball is an ice ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float IceBallChance = 0.15f;

	TWeakObjectPtr<AChaosImpactBall> ActiveBall;
	FTimerHandle SpawnTimer;
};

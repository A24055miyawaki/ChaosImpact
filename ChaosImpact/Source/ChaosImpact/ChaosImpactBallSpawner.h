#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallSpawner.generated.h"

class AChaosImpactBall;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** Training pickup point that replaces its ball after a short delay. */
UCLASS(Blueprintable)
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

	TWeakObjectPtr<AChaosImpactBall> ActiveBall;
	FTimerHandle SpawnTimer;
};


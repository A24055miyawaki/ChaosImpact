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
	/** Set before FinishSpawning: VS stage pads spawn balls outside the training arena too. */
	void SetAlwaysActive(const bool bActive) { bAlwaysActive = bActive; }

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
	float FireBallChance = 0.11f;

	/** Chance (0-1) that a new ball is an ice ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float IceBallChance = 0.11f;

	/** Chance (0-1) that a new ball is a thunder ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ThunderBallChance = 0.08f;

	/** Chance (0-1) that a new ball is a black ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BlackBallChance = 0.08f;

	/** Chance (0-1) that a new ball is a wind ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float WindBallChance = 0.08f;

	/** Chance (0-1) that a new ball is a smoke ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SmokeBallChance = 0.08f;

	/** Chance (0-1) that a new ball is a beam ball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BeamBallChance = 0.08f;

	/** Chance (0-1) that a new ball is a snowball. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SnowBallChance = 0.08f;

	/** Chance (0-1) that a new ball is a nova. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float NovaBallChance = 0.025f;

	/** Chance (0-1) that a new ball releases a shima-enaga flock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SimaeBallChance = 0.05f;

	TWeakObjectPtr<AChaosImpactBall> ActiveBall;
	FTimerHandle SpawnTimer;
	bool bAlwaysActive = false;
};

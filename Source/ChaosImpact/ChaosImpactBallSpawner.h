#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactBallSpawner.generated.h"

class AChaosImpactBall;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * A pickup point that puts out a new ball a few seconds after the last one is taken. Placed in a level it works in
 * training and in solo mode (or anywhere, with Always Active on); VS stages and the title make their own.
 * Which kind of ball comes out is set per pad under "Ball Chances".
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Ball Spawn Point"))
class AChaosImpactBallSpawner : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactBallSpawner();
	virtual void OnConstruction(const FTransform& Transform) override;
	AChaosImpactBall* GetActiveBall() const { return ActiveBall.Get(); }
	/** Set before FinishSpawning: VS stage pads spawn balls outside the training arena too. */
	void SetAlwaysActive(const bool bActive) { bAlwaysActive = bActive; }

	/** How likely (0-1) a new ball from this pad is of this kind, from the weights. */
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball Chances")
	float GetChance(EChaosImpactBallType Type) const;
	/** Changes one kind's weight (0: never). */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Ball Chances")
	void SetChanceWeight(EChaosImpactBallType Type, float Weight);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void TrySpawnBall();

	/** One kind, picked by the weights. */
	EChaosImpactBallType RollBallType() const;
	/** The weight setting for a kind (null for none). */
	float* WeightFor(EChaosImpactBallType Type);
	const float* WeightFor(EChaosImpactBallType Type) const { return const_cast<AChaosImpactBallSpawner*>(this)->WeightFor(Type); }
	void UpdateChanceSummary();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> SpawnPad;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UPointLightComponent> SpawnLight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chaos Impact|Training")
	TSubclassOf<AChaosImpactBall> BallClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(ClampMin="0.25", DisplayName="Respawn Interval (次のボールまでの秒数)"))
	float RespawnInterval = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(DisplayName="Ball Height (ボールの高さ)"))
	float BallHeight = 38.0f;

	/** Puts out balls in any mode (not only training and solo). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Training", meta=(DisplayName="Always Active (どのモードでも出す)"))
	bool bAlwaysActive = false;

	// ---- Ball Chances: weights, compared with each other (they need not add up to anything; 0 is never).
	// For example Normal 3 and Fire 1 (the rest 0): three normal balls to every fire ball. The share each comes to is
	// shown below them.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Normal (普通)"))
	float NormalBallChance = 0.185f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Fire (ファイア)"))
	float FireBallChance = 0.11f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Ice (アイス)"))
	float IceBallChance = 0.11f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Thunder (サンダー)"))
	float ThunderBallChance = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Black (ブラック)"))
	float BlackBallChance = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Wind (ウィンド)"))
	float WindBallChance = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Smoke (スモーク)"))
	float SmokeBallChance = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Beam (ビーム)"))
	float BeamBallChance = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Snow (スノー)"))
	float SnowBallChance = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Nova (ノヴァ)"))
	float NovaBallChance = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Simae (シマエナガ)"))
	float SimaeBallChance = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Chances", meta=(ClampMin="0.0", DisplayName="Drive (ドライブ)"))
	float DriveBallChance = 0.04f;

	/** What the weights come to, in percent (follows them as they change). */
	UPROPERTY(VisibleAnywhere, Transient, Category="Chaos Impact|Ball Chances", meta=(DisplayName="確率の内訳 (%)", MultiLine="true"))
	FString ChanceSummary;

	TWeakObjectPtr<AChaosImpactBall> ActiveBall;
	FTimerHandle SpawnTimer;
};

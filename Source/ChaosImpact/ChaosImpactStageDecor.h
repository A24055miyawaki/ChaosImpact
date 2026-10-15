#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactStageDecor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Things drifting through the air over a stage: snow falling, or motes or wisps drifting. Many small flakes in a box
 * (Area, about the actor), falling and swaying, wrapping round when they leave it. Presentation only.
 */
UCLASS(meta=(DisplayName="Chaos Impact Drifting Flakes (雪・舞うもの)"))
class CHAOSIMPACT_API AChaosImpactDriftingFlakes : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactDriftingFlakes();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Half the size of the box the flakes fill (about the actor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Area (範囲の半分)"))
	FVector Area = FVector(3000.0f, 2500.0f, 900.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Count (数)", ClampMin="1", ClampMax="5000"))
	int32 Count = 900;

	/** cm/s downward (negative rises). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Fall Speed (落ちる速さ)"))
	float FallSpeed = 70.0f;

	/** A steady wind (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Wind (風)"))
	FVector Wind = FVector(15.0f, 25.0f, 0.0f);

	/** How far each flake sways side to side (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Sway (揺れ)"))
	float Sway = 40.0f;

	/** A flake's size (cm), each one between this and half again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Size (大きさ)"))
	float Size = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes", meta=(DisplayName="Material (マテリアル)"))
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Flakes")
	TObjectPtr<UInstancedStaticMeshComponent> Flakes;

private:
	void Seed();
	TArray<FVector> Start;
	TArray<float> Phase;
	float Elapsed = 0.0f;
};

/**
 * A shima-enaga sitting on the stage (a fence post, a branch, the snow): it hops now and then and looks about; when a
 * character comes close it flutters up and away, and comes back a while later. Presentation only (no collision).
 */
UCLASS(meta=(DisplayName="Chaos Impact Shima-enaga (シマエナガの飾り)"))
class CHAOSIMPACT_API AChaosImpactSimaePerch : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaePerch();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The bird's size (1: about a hand's length, like the ball's birds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Shima-enaga", meta=(DisplayName="Scale (大きさ)", ClampMin="0.1"))
	float BirdScale = 1.2f;

	/** A character this close sends it flying. 0: it never flies off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Shima-enaga", meta=(DisplayName="Shy Distance (逃げる距離)"))
	float ShyDistance = 380.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Shima-enaga")
	TObjectPtr<USceneComponent> Body;

private:
	void BuildBird();
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	TObjectPtr<UStaticMeshComponent> Wing;
	FVector Home = FVector::ZeroVector;
	FRotator HomeRotation = FRotator::ZeroRotator;
	float Clock = 0.0f;
	float NextHop = 1.0f;
	float HopTime = -1.0f;
	float Look = 0.0f;
	float LookTarget = 0.0f;
	/** < 0 sitting; 0.. flying away, then gone, then landing back. */
	float FlightTime = -1.0f;
	FVector FlightWay = FVector::ZeroVector;
};

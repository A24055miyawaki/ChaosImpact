#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactSimaeBird.generated.h"

class AChaosImpactCharacter;
class APawn;
class USceneComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
struct FChaosImpactSimaeFlockState;

UENUM()
enum class EChaosImpactSimaeBirdState : uint8
{
	PopOut,
	Hunting,
	Perched,
	Departing
};

/** One bird from a shima-enaga ball. The server flies and resolves it; clients reproduce the movement and animation. */
UCLASS()
class AChaosImpactSimaeBird final : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeBird();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: releases one flock and distributes its birds across nearby viable enemies. */
	static void ReleaseFlock(UWorld* World, const FVector& Location, APawn* ThrowingPawn, AActor* DirectVictim = nullptr);
	/** Used by CPUs and the HUD to know that a dash can shake the flock off. */
	static bool HasPerchedBird(const AChaosImpactCharacter* Character, float* OutRemainingSeconds = nullptr,
		float* OutCountdownSeconds = nullptr, int32* OutPecksRemaining = nullptr);

	EChaosImpactSimaeBirdState GetBirdState() const { return BirdState; }
	AChaosImpactCharacter* GetTargetCharacter() const { return TargetCharacter; }
	int32 GetFlockId() const { return FlockId; }
	int32 GetBirdIndex() const { return BirdIndex; }

private:
	void ConfigureBird(int32 InFlockId, int32 InBirdIndex, APawn* InThrower,
		AChaosImpactCharacter* InTarget, const FVector& InVelocity, const TSharedPtr<FChaosImpactSimaeFlockState>& InFlockState);
	void BuildAppearance();
	void UpdateAppearance(float DeltaSeconds);
	void UpdateServer(float DeltaSeconds);
	void BeginHunting();
	void BeginPerch();
	void BeginDeparting(const FVector& AwayDirection);
	void ReleasePerchedGroup(bool bPecked);
	AChaosImpactCharacter* FindTarget() const;
	FVector GetTargetHeadLocation() const;
	bool IsValidTarget(const AChaosImpactCharacter* Candidate) const;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastChirp(uint8 Moment);
	void PlayChirp(uint8 Moment) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> TailMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WingMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> EyeMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CrestMesh;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> FlightTrail;

	struct FTrailPoint
	{
		FVector Position = FVector::ZeroVector;
		float Time = 0.0f;
	};
	TArray<FTrailPoint> TrailPoints;

	UPROPERTY(Replicated)
	EChaosImpactSimaeBirdState BirdState = EChaosImpactSimaeBirdState::PopOut;

	UPROPERTY(Replicated)
	TObjectPtr<AChaosImpactCharacter> TargetCharacter;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> SourcePawn;

	UPROPERTY(Replicated)
	int32 FlockId = 0;

	UPROPERTY(Replicated)
	int32 BirdIndex = 0;

	/** The flock shares one schedule per enemy: each peck is one damage, at most two, never one hit per bird. */
	UPROPERTY(Replicated)
	float FirstPerchedAt = -1.0f;

	UPROPERTY(Replicated)
	float NextPeckAt = -1.0f;

	UPROPERTY(Replicated)
	float LastPeckAt = -1.0f;

	UPROPERTY(Replicated)
	int32 PecksDelivered = 0;

	FVector Velocity = FVector::ZeroVector;
	FVector SpawnLocation = FVector::ZeroVector;
	float SpawnedAt = 0.0f;
	float StateStartedAt = 0.0f;
	float VisualTime = 0.0f;
	float NextTargetReviewAt = 0.0f;
	TSharedPtr<FChaosImpactSimaeFlockState> FlockState;
};

/** Render-only feather burst: spawned on every relevant screen by the flock's multicast event. */
UCLASS()
class AChaosImpactSimaeFeatherBurst final : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeFeatherBurst();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	static void Play(UWorld* World, const FVector& Location, float Scale);

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> BurstRoot;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> ShockRing;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> BurstLight;

	struct FFeather
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FVector Velocity = FVector::ZeroVector;
		FVector Spin = FVector::ZeroVector;
		float Size = 1.0f;
	};
	TArray<FFeather> Feathers;
	float BurstScale = 1.0f;
	float Age = 0.0f;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactTornado.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * The wind ball's tornado: released the moment the ball leaves the hand, it weaves forward in the throw's direction,
 * rebounding off walls, for a few seconds. Anyone it catches (not the thrower or their team) takes one hit and is
 * whirled round it and thrown out, once per tornado.
 *
 * Its path is a function of the server time it started at, so every machine draws it in the same place without
 * sending its position. The server damages characters it moves itself (the host, CPUs); a remote player's own
 * screen judges their contact, blows them away there and reports it, like a ball hit.
 */
UCLASS()
class AChaosImpactTornado : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactTornado();

	/** Server: starts a tornado at From (moved down to the floor) heading along Direction. */
	static AChaosImpactTornado* Release(UWorld* World, const FVector& From, const FVector& Direction, APawn* Source);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: a remote player's screen saw this tornado catch them. True when it counted. */
	bool TryApplyReportedHit(AChaosImpactCharacter* Victim);

	APawn* GetSourcePawn() const { return SourcePawn; }
	/** The way it is heading now (before its weave), on this machine. */
	FVector GetTravelDirection() const { return PathHeading; }
	/** The foot of the funnel now, on this machine. */
	FVector GetCenter() const { return Center; }
	bool IsActive() const { return bActive; }

	static constexpr float ActiveSeconds = 4.0f;
	static constexpr float CollapseSeconds = 0.45f;
	static constexpr float TravelSpeed = 430.0f;
	static constexpr float CatchRadius = 150.0f;
	static constexpr float FunnelHeight = 430.0f;
	/** A ball flying into the funnel is flung round and out at least this fast. */
	static constexpr float DeflectSpeed = 1500.0f;
	/** At most this many lying balls are carried at once. */
	static constexpr int32 MaxCarriedBalls = 8;

private:
	void StepPath(float Seconds);
	void CatchCharacters();
	bool CanCatch(const AActor* Victim) const;
	double GetServerNow() const;
	/** Server: turns flying balls that enter the funnel, sweeps up lying ones and carries them round. */
	void CatchBalls(float DeltaSeconds);
	/** Server: drops every carried ball around the funnel, flung outward. */
	void ReleaseCarriedBalls();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastGust(FVector_NetQuantize Location, bool bBig);

	void BuildPresentation();
	void UpdatePresentation(float Age, float DeltaSeconds);
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, UMaterialInstanceDynamic* Material);

	UPROPERTY(Replicated)
	FVector_NetQuantize Origin;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal Heading;

	UPROPERTY(Replicated)
	double StartServerTime = 0.0;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> SourcePawn;

	UPROPERTY(Replicated)
	int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Path simulated so far on this machine (a fixed step, so every machine lands on the same points). */
	FVector PathPosition = FVector::ZeroVector;
	FVector PathHeading = FVector::ForwardVector;
	float SimulatedSeconds = 0.0f;
	bool bPathStarted = false;
	FVector Center = FVector::ZeroVector;
	bool bActive = true;
	/** Characters and targets already caught, on this machine. */
	TSet<TWeakObjectPtr<AActor>> Caught;
	/** Server: flying balls already turned by this tornado. */
	TSet<TWeakObjectPtr<AChaosImpactBall>> Deflected;
	struct FCarriedBall
	{
		TWeakObjectPtr<AChaosImpactBall> Ball;
		float Angle = 0.0f;
		float Height = 0.0f;
		float Age = 0.0f;
	};
	TArray<FCarriedBall> CarriedBalls;
	bool bBallsReleased = false;
	float NextWindLogAt = 0.0f;

	// Presentation.
	bool bPresentationBuilt = false;
	float PresentationAge = 0.0f;
	double NextDustAt = 0.0;
	TWeakObjectPtr<UProceduralMeshComponent> Funnel;
	TWeakObjectPtr<UProceduralMeshComponent> GroundRings;
	TWeakObjectPtr<UMaterialInstanceDynamic> FunnelMaterial;
	TWeakObjectPtr<UMaterialInstanceDynamic> RingMaterial;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Shells;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> ShellMaterials;
	struct FDebris
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		float Angle = 0.0f;
		float Height = 0.0f;
		float Speed = 1.0f;
		float Size = 1.0f;
	};
	TArray<FDebris> Debris;
	TWeakObjectPtr<UNiagaraComponent> BaseDust;
	TWeakObjectPtr<UNiagaraComponent> Sparks;
	TWeakObjectPtr<UPointLightComponent> Light;
	FVector LastCenter = FVector::ZeroVector;
};

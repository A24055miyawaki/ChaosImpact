#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "ChaosImpactWarpPad.generated.h"

class ACharacter;
class AChaosImpactCharacter;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** A player charging up on a pad, with the effects around them on this machine. */
struct FChaosImpactWarpCharge
{
	double StartedAt = 0.0;
	TWeakObjectPtr<UNiagaraComponent> Aura;
	TWeakObjectPtr<UStaticMeshComponent> Haze;
	TWeakObjectPtr<UMaterialInstanceDynamic> HazeMaterial;
	TWeakObjectPtr<UStaticMeshComponent> Rings[2];
	TWeakObjectPtr<UMaterialInstanceDynamic> RingMaterials[2];
};

/** The pillar of light and shock ring where a player leaves or arrives. */
struct FChaosImpactWarpBurst
{
	double StartedAt = 0.0;
	FVector Base = FVector::ZeroVector;
	bool bArrival = false;
	TWeakObjectPtr<UStaticMeshComponent> Beam;
	TWeakObjectPtr<UMaterialInstanceDynamic> BeamMaterial;
	TWeakObjectPtr<UStaticMeshComponent> Ring;
	TWeakObjectPtr<UMaterialInstanceDynamic> RingMaterial;
};

/**
 * Warp device. A player who stays on top of it for WarpChargeSeconds is sent to one of the other pads, picked at
 * random among pads with the same group and the same parent (pads inside the VS stage only link to each other).
 * Players climb it by its steps. While they charge, a haze swells around them; the warp itself shoots them up in a
 * pillar of light that comes down on the destination.
 * The server decides and moves the player (online members included); every machine plays the effects.
 * Players arriving on a pad do not charge again until they have stepped off it.
 *
 * Place BP_WarpPad in a level, or as a child actor in BP_VersusStage.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Warp Pad"))
class AChaosImpactWarpPad : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactWarpPad();
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Height of the top surface players stand on, above the actor origin. */
	float GetStandHeight() const { return SurfaceHeight; }
	/** On the top surface, near enough to the middle. */
	bool IsStandingOnPad(const ACharacter* Character) const;
	/** Middle of the raised top, on the floor. The model's steps push it a little off the actor origin. */
	FVector GetPlatformCentre() const;
	/** 0-1 while the character charges on any pad (as seen on this machine), 0 otherwise. */
	static float FindChargeProgress(const AActor* Character);

	/** Pads only send players to other pads with the same group (and the same parent actor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Chaos Impact|Warp")
	int32 WarpGroup = 0;

	/** Seconds a player has to stay on top before being sent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Warp", meta=(ClampMin="0.0"))
	float WarpChargeSeconds = 2.5f;

	/** Scale the model to PadDiameter wide and ModelHeight tall. Off keeps the scale set on the mesh component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Warp")
	bool bFitModelToDiameter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Warp", meta=(ClampMin="40.0"))
	float PadDiameter = 720.0f;

	/** Height of the whole model, posts included. Keep each of its steps under about 45 so they can be walked up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Warp", meta=(ClampMin="10.0"))
	float ModelHeight = 180.0f;

	/** How far from the middle of the raised top a player still charges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Warp", meta=(ClampMin="10.0"))
	float TriggerRadius = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Warp")
	FLinearColor GlowColor = FLinearColor(0.12f, 0.72f, 1.0f);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Stops the charge, marks the arrival and plays departure and arrival effects on every machine. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastWarpEffects(AChaosImpactCharacter* Character, FVector_NetQuantize From, FVector_NetQuantize To,
		AChaosImpactWarpPad* Destination);

	void TryWarp(AChaosImpactCharacter* Character);
	AChaosImpactWarpPad* ChooseDestination() const;
	/** Fits the model and places the light. */
	void LayoutPad();
	/** Finds the top surface on the fitted model. */
	void MeasureSurface();
	/** Sizes the invisible step over the raised top. */
	void LayoutStep();
	/** Lights the pad up briefly after a warp from or to it. */
	void Flare() { GlowBoost = 1.0f; }

	void StartChargeVisuals(FChaosImpactWarpCharge& Charge, AChaosImpactCharacter* Character);
	void UpdateChargeVisuals(const FChaosImpactWarpCharge& Charge, const AChaosImpactCharacter* Character, float Progress, double Now) const;
	void StopChargeVisuals(FChaosImpactWarpCharge& Charge) const;
	void SpawnBurst(const FVector& Base, bool bArrival);
	void UpdateBursts(double Now);
	UStaticMeshComponent* MakeEffectMesh(float RimOnly, UMaterialInstanceDynamic*& OutMaterial);

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** The warp device model (the imported Warp mesh unless replaced). Its own collision is walked on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> PadMesh;

	/** Invisible round block over the raised top: its sloped edge is hard to walk onto, a plain step is easy. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> StepCollision;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> PadLight;

	UPROPERTY()
	TObjectPtr<UStaticMesh> EffectCylinder;

	/** Players on top charging, on every machine. */
	TMap<TWeakObjectPtr<AActor>, FChaosImpactWarpCharge> Charges;
	TArray<FChaosImpactWarpBurst> Bursts;
	/** Players who arrived (or just left) and have not stepped off yet, with when. */
	TMap<TWeakObjectPtr<AActor>, double> ArrivedAt;
	float SurfaceHeight = 98.0f;
	/** Raised top, relative to the actor, after fitting. */
	float PlatformOffsetY = 0.0f;
	float PlatformRadius = 250.0f;
	float GlowBoost = 0.0f;
};

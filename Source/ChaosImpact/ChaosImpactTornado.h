#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactFusionEffects.h"
#include "ChaosImpactTornado.generated.h"

class AChaosImpactBall;
struct FChaosImpactDuelThrowDamage;
class AChaosImpactCharacter;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UAudioComponent;
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
	static AChaosImpactTornado* Release(UWorld* World, const FVector& From, const FVector& Direction, APawn* Source,
		EChaosImpactBallType Fused = EChaosImpactBallType::Normal, EChaosImpactBallType Tint = EChaosImpactBallType::Normal,
		float TintAmount = 0.0f);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	/** One of its hits: a nova's with a ノヴァ riding along (2026-11-19), else 1. */
	float GetNovaRiderDamage() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Any machine, look only (the RPG mode's wind ball): the same funnel, standing wherever SetVisualCenter puts it,
	 * for Seconds. It catches no one and no ball, and is not replicated. Scale sizes the whole funnel.
	 */
	static AChaosImpactTornado* SpawnVisual(UWorld* World, const FVector& At, float Seconds, float Scale = 1.0f);
	void SetVisualCenter(const FVector& At) { VisualCenter = At; }
	/** Look only: collapses now. */
	void EndVisual();
	bool IsVisualOnly() const { return bVisualOnly; }
	/** Server: a remote player's screen saw this tornado catch them. True when it counted. */
	bool TryApplyReportedHit(AChaosImpactCharacter* Victim);

	APawn* GetSourcePawn() const { return SourcePawn; }
	TSharedPtr<FChaosImpactDuelThrowDamage> GetDuelThrowDamage() const { return DuelThrowDamage; }
	/** The way it is heading now (before its weave), on this machine. */
	FVector GetTravelDirection() const { return PathHeading; }
	/** The foot of the funnel now, on this machine. */
	FVector GetCenter() const { return Center; }
	bool IsActive() const { return bActive; }
	/** How fast it goes (雷雲竜巻: as fast as thunder). */
	float GetTravelSpeed() const { return TravelSpeed * SpeedScale; }
	/** Server, right after Release: a fused wind ball's second kind (合体ボール): along its way and where it ends. */
	void SetFusedType(const EChaosImpactBallType Type) { FusedType = Type; }
	/** Any machine: a black hole riding along on it (大渦竜巻): it draws in whoever is near as it goes. */
	bool IsFusedPullActive() const { return FusedType == EChaosImpactBallType::Black && bActive && !bVisualOnly; }
	static constexpr float FusedPullRadius = 450.0f;

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
	TSharedPtr<FChaosImpactDuelThrowDamage> DuelThrowDamage;
	UPROPERTY(Replicated)
	EChaosImpactBallType FusedType = EChaosImpactBallType::Normal;
	/** 合体ボール: its colours turned toward this kind's (percent). */
	UPROPERTY(Replicated)
	EChaosImpactBallType TintType = EChaosImpactBallType::Normal;
	/** 雷雲竜巻: how many times as fast as a tornado it goes (and how much straighter); the same on every machine. */
	UPROPERTY(Replicated)
	float SpeedScale = 1.0f;
	static constexpr float ThunderSpeedScale = 3.5f;
	UPROPERTY(Replicated)
	uint8 TintPercent = 0;
	/** One of its colours as shown (turned by TintType). */
	FLinearColor Hue(const FLinearColor& Color) const
	{
		return TintType == EChaosImpactBallType::Normal || TintPercent == 0 ? Color
			: ChaosImpactFusion::TintToward(Color, TintType, TintPercent / 100.0f);
	}
	/** Fused: its look on the funnel, the trail puffs, a fire's line along the way, the second effect once at the end. */
	void TickFusion(float Age, float DeltaSeconds);
	ChaosImpactFusion::FFusionLook FusionLook;
	float FusionPuffAt = 0.0f;
	bool bFusionApplied = false;
	ChaosImpactFusion::FFusionCarry FusionCarry;
	/** Server: whoever it caught gets what rides along (once each). */
	void StrikeFused(class AChaosImpactCharacter* Victim);
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
	/** Puffs of dust whipped up at At (soft FX-kit smoke). */
	void TornadoDust(const FVector& At, int32 Puffs, float Scale);
	/** Its soft FX-kit pieces (rings rolling out, the lit ground), riding with it. */
	TWeakObjectPtr<class AChaosImpactFxKit> KitFx;
	/** How long it whirls (ActiveSeconds, or a look-only funnel's own time). */
	float GetWhirlSeconds() const { return bVisualOnly ? VisualSeconds : ActiveSeconds; }
	bool bVisualOnly = false;
	float VisualSeconds = 0.0f;
	double VisualStartedAt = 0.0;
	FVector VisualCenter = FVector::ZeroVector;
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

	/** The roar of the wind while it whirls (it moves with the tornado). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WindSound;

	/** Path simulated so far on this machine (a fixed step, so every machine lands on the same points). */
	FVector PathPosition = FVector::ZeroVector;
	FVector PathHeading = FVector::ForwardVector;
	float SimulatedSeconds = 0.0f;
	bool bPathStarted = false;
	FVector Center = FVector::ZeroVector;
	bool bActive = true;
	/** Characters and targets already caught, on this machine. */
	/** Who it has caught, and when (a fused one catches again after a while, outside the one-hit rule: 2026-11-19). */
	TMap<TWeakObjectPtr<AActor>, double> Caught;
	bool IsStillCaught(const AActor* Victim) const;
	void MarkCaught(AActor* Victim);
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

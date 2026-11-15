#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactGadget.generated.h"

class AChaosImpactCharacter;
class AChaosImpactFxKit;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** What a gadget is and how far along (all a client needs to draw it and judge its own player). */
USTRUCT()
struct FChaosImpactGadgetState
{
	GENERATED_BODY()
	/** EChaosImpactBallType::Barrier or ::Mine. */
	UPROPERTY() uint8 Kind = 0;
	/** Server times: put down, set off (a mine's fuse starts; 0: not yet), gone (burst, broken or timed out; 0: not yet). */
	UPROPERTY() double PlacedAt = 0.0;
	UPROPERTY() double TriggeredAt = 0.0;
	UPROPERTY() double EndedAt = 0.0;
	/** Gone with a bang: a mine's blast, a barrier broken by a ball (else it faded away). */
	UPROPERTY() bool bBurst = false;
};

/**
 * What the バリア and マイン balls leave where they land. Light on the line and the machine: it never moves (its place is
 * sent once), its state changes two or three times in its life, and only a mine looks about itself, ten times a second.
 *
 *  - バリア: a low wall of light across the ball's way for a few seconds. Balls stop at it (hooks and anchors stick in
 *    it); people walk through. The first ball to strike it breaks it.
 *  - マイン: lies on the floor in plain sight. Armed a moment after landing; an opponent coming near sets it off, and
 *    after a short fuse (its reach shown on the floor) it bursts: 1 damage and a push to opponents in reach (a dash
 *    gets clear). Its owner and their team neither set it off nor are hurt. Each player has two at most.
 *
 * Hits are judged as the game's others are: the host for its own players and the CPUs, a guest on their own screen.
 */
UCLASS(NotPlaceable, Transient)
class CHAOSIMPACT_API AChaosImpactGadget : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactGadget();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: put down on Floor by Source's ball (a barrier stands across Heading). */
	static AChaosImpactGadget* Place(UWorld* World, EChaosImpactBallType Type, const FVector& Floor, const FVector& Heading, APawn* Source);
	/** Server: the floor under (or near under) a ball's landing, if there is one. */
	static bool FindFloor(const UWorld* World, const FVector& Near, FVector& OutFloor);

	bool IsBarrier() const { return State.Kind == static_cast<uint8>(EChaosImpactBallType::Barrier); }
	bool IsMine() const { return State.Kind == static_cast<uint8>(EChaosImpactBallType::Mine); }
	bool HasEnded() const { return State.EndedAt > 0.0; }
	const FChaosImpactGadgetState& GetState() const { return State; }
	APawn* GetSourcePawn() const { return Source; }
	/** Server: a ball thrown by Thrower has struck the barrier (it breaks). */
	void AbsorbBall(APawn* Thrower);
	/** Server: whose ball broke the barrier (null: not broken by a ball, or that thrower is gone). */
	APawn* GetBrokenBy() const { return BrokenBy.Get(); }
	/** Server: every gadget gone (a round or lesson starts afresh). */
	static void ClearAll(UWorld* World);
	/** Server: a guest's own screen saw the mine's blast reach its player. */
	bool TryApplyReportedBlast(AChaosImpactCharacter* Victim);

	// バリア
	static constexpr float BarrierWidth = 320.0f;
	static constexpr float BarrierHeight = 150.0f;
	static constexpr float BarrierThickness = 30.0f;
	static constexpr float BarrierSeconds = 8.0f;
	static constexpr float BarrierRiseSeconds = 0.22f;
	// マイン
	static constexpr float MineArmSeconds = 0.8f;
	static constexpr float MineTriggerRadius = 220.0f;
	static constexpr float MineFuseSeconds = 0.6f;
	static constexpr float MineBlastRadius = 380.0f;
	static constexpr float MineSeconds = 20.0f;
	static constexpr int32 MinesPerPlayer = 2;
	static constexpr float MineKnockback = 1050.0f;
	/** Gone from every screen this long after it ended (its last effect plays out). */
	static constexpr float FadeSeconds = 0.8f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_State)
	FChaosImpactGadgetState State;
	UPROPERTY(Replicated)
	TObjectPtr<APawn> Source;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Block;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMaterial;
	TWeakObjectPtr<AChaosImpactFxKit> Fx;

	UFUNCTION()
	void OnRep_State();
	double GetNow() const;
	void End(bool bBurst);
	void ServerTickMine(double Now);
	void Burst(double Now);
	bool CanHurt(const AChaosImpactCharacter* Victim) const;
	bool IsInReach(const AChaosImpactCharacter* Victim, float Radius) const;
	void ApplyBlast(AChaosImpactCharacter* Victim);
	void BuildLook();
	void UpdateLook(double Now, float DeltaSeconds);
	void PlayEndEffect();
	void JudgeOwnPlayers();
	AChaosImpactFxKit* GetFx();

	/** Server: who the blast has hurt (each once), and when the mine next looks about itself. */
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> Hurt;
	TWeakObjectPtr<APawn> BrokenBy;
	double NextLookAt = 0.0;
	/** Every machine: what has been shown of it. */
	bool bLookBuilt = false;
	bool bEndShown = false;
	bool bTriggerShown = false;
	bool bArmShown = false;
	bool bOwnJudged = false;
	double NextRingAt = 0.0;
};

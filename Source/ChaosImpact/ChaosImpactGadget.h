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
	/** 合体 (2026-11-19): a barrier's other kind (炎の壁, 電撃バリア…; EChaosImpactBallType, 0 none) and how long it stands (0: as ever). */
	UPROPERTY() uint8 Variant = 0;
	UPROPERTY() float LifeSeconds = 0.0f;
	/** 回復スポット (2026-11-19): heals given (0-2), how far the stay for the second has come (percent), who was healed last. */
	UPROPERTY() uint8 Heals = 0;
	UPROPERTY() uint8 StayPercent = 0;
	UPROPERTY() TObjectPtr<AChaosImpactCharacter> Healed;
};

/**
 * What the バリア and マイン balls leave where they land. Light on the line and the machine: it never moves (its place is
 * sent once), its state changes two or three times in its life, and only a mine looks about itself, ten times a second.
 *
 *  - バリア: a low wall of light across the ball's way for a few seconds (2026-11-16 rules). Its thrower (and their team)
 *    walk through it and their balls fly through it. An opponent's ball stops at it and breaks it (hooks and anchors
 *    stick in it); an opponent walking into it takes 1, is thrown back, and breaks it (one already standing where it
 *    came up is let out, and a dash slips through).
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
	static AChaosImpactGadget* Place(UWorld* World, EChaosImpactBallType Type, const FVector& Floor, const FVector& Heading, APawn* Source,
		EChaosImpactBallType FusionTint = EChaosImpactBallType::Normal, float FusionTintAmount = 0.0f, bool bBeyondLimit = false);
	/** Server: the floor under (or near under) a ball's landing, if there is one. */
	static bool FindFloor(const UWorld* World, const FVector& Near, FVector& OutFloor);

	bool IsBarrier() const { return State.Kind == static_cast<uint8>(EChaosImpactBallType::Barrier); }
	bool IsMine() const { return State.Kind == static_cast<uint8>(EChaosImpactBallType::Mine); }
	/** The heal ball's spot (solo mode). */
	bool IsHeal() const { return State.Kind == static_cast<uint8>(EChaosImpactBallType::Heal); }
	/** Who the spot heals: its thrower and their team, standing, not shut in a prison ball. */
	bool CanHeal(const AChaosImpactCharacter* Who) const;
	bool HasEnded() const { return State.EndedAt > 0.0; }
	/** Server: a mine left by a fused ball (合体ボール): it bursts with this kind's effect as well. */
	void SetFusedWith(const EChaosImpactBallType Type) { FusedWith = Type; }
	/** Server: a barrier joined with With (2026-11-19): standing LifeScale as long, taking Hits balls before it breaks. */
	void SetBarrierVariant(EChaosImpactBallType With, float LifeScale, int32 Hits);
	EChaosImpactBallType GetVariant() const { return static_cast<EChaosImpactBallType>(State.Variant); }
	/** 電撃バリア: a ball it stops goes back the way it came, its owner's now. */
	bool TurnsBallsBack() const { return IsBarrier() && GetVariant() == EChaosImpactBallType::Thunder; }
	/** How long this barrier stands. */
	float GetBarrierLife() const { return State.LifeSeconds > 0.0f ? State.LifeSeconds : BarrierSeconds; }
	const FChaosImpactGadgetState& GetState() const { return State; }
	APawn* GetSourcePawn() const { return Source; }
	/** Server: a ball thrown by Thrower has struck the barrier (it breaks). */
	void AbsorbBall(APawn* Thrower);
	/** Server: whose ball broke the barrier (null: not broken by a ball, or that thrower is gone). */
	APawn* GetBrokenBy() const { return BrokenBy.Get(); }
	/** Server: every gadget gone (a round or lesson starts afresh). */
	static void ClearAll(UWorld* World);
	/** Server: a guest's own screen saw the mine's blast reach its player (or its player walk into the barrier). */
	bool TryApplyReportedBlast(AChaosImpactCharacter* Victim);
	/** Who is against the barrier's wall (their body in it, below its top). */
	bool IsTouchingBarrier(const AChaosImpactCharacter* Who) const;

	// バリア
	static constexpr float BarrierWidth = 320.0f;
	static constexpr float BarrierHeight = 150.0f;
	static constexpr float BarrierThickness = 30.0f;
	static constexpr float BarrierSeconds = 8.0f;
	static constexpr float BarrierRiseSeconds = 0.22f;
	/** An opponent walking into it: thrown back this hard (and 1 damage). */
	static constexpr float BarrierKnockback = 900.0f;
	// マイン
	static constexpr float MineArmSeconds = 0.8f;
	static constexpr float MineTriggerRadius = 220.0f;
	static constexpr float MineFuseSeconds = 0.6f;
	static constexpr float MineBlastRadius = 380.0f;
	static constexpr float MineSeconds = 20.0f;
	static constexpr int32 MinesPerPlayer = 2;
	static constexpr float MineKnockback = 1050.0f;
	// 回復スポット (the heal ball, solo only): stepping in heals 1 at once; staying on HealStaySeconds more heals 1 more and
	// it is gone. Only someone hurt is healed (a full one leaves it for later); unused it fades after HealSeconds.
	static constexpr float HealRadius = 150.0f;
	static constexpr float HealArmSeconds = 0.3f;
	static constexpr float HealStaySeconds = 2.5f;
	static constexpr float HealSeconds = 25.0f;
	static constexpr int32 HealsPerSpot = 2;
	/** Gone from every screen this long after it ended (its last effect plays out). */
	static constexpr float FadeSeconds = 0.8f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_State)
	FChaosImpactGadgetState State;
	UPROPERTY(Replicated)
	TObjectPtr<APawn> Source;
	/** 合体ボール: its look turned toward the other kind's colour (percent). */
	UPROPERTY(Replicated)
	EChaosImpactBallType FusionTint = EChaosImpactBallType::Normal;
	UPROPERTY(Replicated)
	uint8 FusionTintPercent = 0;
	EChaosImpactBallType FusedWith = EChaosImpactBallType::Normal;

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
	void ServerTickBarrier(double Now);
	/** Server (合体 barriers): balls left before it breaks; when it last looked about (見張りのバリア's birds, 吸引バリア's pull). */
	int32 HitsLeft = 1;
	int32 Watches = 0;
	double NextWatchAt = 0.0;
	double LastBarrierTickAt = 0.0;
	void BreakOnWalker(AChaosImpactCharacter* Victim);
	void JudgeOwnWalkers(double Now);
	/** Who was already in the wall when it came up (let out without harm until they have left it), here and on a guest. */
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> InsideAtRise;
	bool bWalkReported = false;
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

	// 回復スポット (ChaosImpactGadgetHeal.cpp).
	void ServerTickHeal(double Now, float DeltaSeconds);
	void GiveHeal(AChaosImpactCharacter* Who, double Now);
	void BuildHealLook();
	void UpdateHealLook(double Now, float DeltaSeconds);
	void PlayHealOn(AChaosImpactCharacter* Who);
	void PlayHealEnd();
	/** Server: how long someone hurt has stood on it since the first heal. */
	float HealStay = 0.0f;
	/** Every machine: heals shown so far; its parts (the fill, the filling stay, the heart); when the next sparkle rises. */
	uint8 ShownHeals = 0;
	int32 HealFillPart = INDEX_NONE;
	int32 HealStayPart = INDEX_NONE;
	int32 HealHeartFrom = INDEX_NONE;
	/** The heart and the stay's arc, turned each frame to the player's camera (the heart's point always down the screen). */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HealPivot;
	int32 HealRingFrom = INDEX_NONE;
	double NextSparkleAt = 0.0;
};

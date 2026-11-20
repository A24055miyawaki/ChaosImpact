#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactFusionEffects.generated.h"

class AChaosImpactCharacter;
class AChaosImpactHazardZone;
class APawn;
class UWorld;
struct FChaosImpactDuelThrowDamage;

/**
 * 合体ボール's own effects, pair by pair (2026-11-17; Docs/合体ボール専用効果企画.md §3). What flies (its way of flying:
 * steered, piercing, rebounding, a hook, an anchor, a tornado, a rolled snowball, a flock, a swap) carries what rides
 * along, and the rider shows itself at each moment of the flight:
 *  - on the way (TickCarry): fires, frozen floor, smoke left behind; a crackle that strikes someone near (after a warning);
 *    a whirl that pushes aside; mines dropped (a beam's);
 *  - on whoever it strikes without bursting (OnStrike: a beam piercing, a steered drive, a hook's catch, an anchor's ram,
 *    a tornado's catch, a snowball rolled over someone, a bird perching): set burning, frozen, blinded, shocked, blown
 *    aside, the others near drawn to them, frozen in snow, swapped, a mine stuck on, a flock on them;
 *  - each time it rebounds (OnBounce: thunder);
 *  - where it ends (ApplyEndVariant, before the rider's plain effect): steam (fire with ice), a burning whirl, a frozen
 *    whirl, a thundering whirl, a storm cloud, a swap's effect where the other lands;
 *  - a mine it leaves bursts with the flier's effect (ApplyMineVariant).
 * One hit is still one damage: what is added is hazards (fires, lightning, black holes) and states, never a second hit.
 * Everything is the server's: replicated zones, tornadoes, gadgets and the strikes' warnings (AChaosImpactFusionStrike).
 */
class AChaosImpactGadget;

namespace ChaosImpactFusion
{
	struct FFusionCarry
	{
		FVector LastDrop = FVector::ZeroVector;
		bool bStarted = false;
		int32 Drops = 0;
		int32 Mines = 0;
		int32 Zaps = 0;
		int32 Bounces = 0;
		bool bSwapped = false;
		bool bFlock = false;
		double NextZapAt = 0.0;
		TWeakObjectPtr<AChaosImpactHazardZone> FireLeader;
		TSet<TWeakObjectPtr<AActor>> Touched;
	};

	/** Server, every frame something fused flies (At: where it is now; Reach: its snow's size). */
	CHAOSIMPACT_API void TickCarry(UWorld* World, EChaosImpactBallType Flier, EChaosImpactBallType Rider, const FVector& At, APawn* Source,
		TSharedPtr<FChaosImpactDuelThrowDamage> Damage, FFusionCarry& Carry, float Reach = 1.0f);
	/** The rider does something of its own to whoever is struck (so a hook's catch shows it, not a burst at the hook). */
	CHAOSIMPACT_API bool IsStrikeRider(EChaosImpactBallType Rider);
	/** Server: something fused struck Victim without bursting (heading: the way it was going). */
	CHAOSIMPACT_API void OnStrike(UWorld* World, EChaosImpactBallType Flier, EChaosImpactBallType Rider, AChaosImpactCharacter* Victim,
		const FVector& Heading, APawn* Source, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, FFusionCarry* Carry = nullptr);
	/** Server: something fused rebounded off a wall at At. */
	CHAOSIMPACT_API void OnBounce(UWorld* World, EChaosImpactBallType Flier, EChaosImpactBallType Rider, const FVector& At, APawn* Source,
		TSharedPtr<FChaosImpactDuelThrowDamage> Damage, FFusionCarry& Carry);
	/** Server: the pair's own end, where it has one. True when it did all there is (the rider's plain effect is not added). */
	CHAOSIMPACT_API bool ApplyEndVariant(UWorld* World, EChaosImpactBallType Flier, EChaosImpactBallType Rider, const FVector& At,
		APawn* Source, AActor* Victim, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, float Reach);
	/** Server: a mine left by a fused ball bursts at At (Victim: who set it off): the flier's effect goes off with it. */
	CHAOSIMPACT_API void ApplyMineVariant(UWorld* World, EChaosImpactBallType Flier, const FVector& At, APawn* Source, AChaosImpactCharacter* Victim);
	/** Whoever may be struck by Source's fused things (not them, not their team, not out). */
	CHAOSIMPACT_API bool IsFusionTarget(const UWorld* World, const AActor* Candidate, const APawn* Source);

	// ---- 2026-11-19 (Docs/合体ボール追加企画.md, ChaosImpactFusionPlus.cpp): ノヴァ, プリズン, バリア, グラップル, 強化ボール.
	/** Server: Victim shut in a prison shell by Source (where prisons are played). */
	CHAOSIMPACT_API bool Imprison(AChaosImpactCharacter* Victim, APawn* Source);
	/** Server: up to Most of Source's targets from MinDistance to Radius of At shut in shells (nearest first). */
	CHAOSIMPACT_API int32 ImprisonAround(UWorld* World, const FVector& At, float Radius, APawn* Source, int32 Most, float MinDistance = 0.0f,
		const AActor* Except = nullptr);
	/**
	 * Server: a barrier joined with With (炎の壁, 氷の壁, 電撃バリア…) on the floor near Near, across Heading. Some are more
	 * than one wall (長い光の壁: three in a line along the way). Returns the first.
	 */
	CHAOSIMPACT_API AChaosImpactGadget* PlaceFusedBarrier(UWorld* World, const FVector& Near, const FVector& Heading, APawn* Source,
		EChaosImpactBallType With, float LifeScale = 1.0f, int32 Hits = 1);
	/** How big a ノヴァ riding along on Source's last throw was charged (its blast's scale). */
	CHAOSIMPACT_API float GetNovaRideScale(const APawn* Source);
	/** Server: the new pairs' and the 強化ボール's own ends. True when it did all there is. */
	CHAOSIMPACT_API bool ApplyPlusEnd(UWorld* World, EChaosImpactBallType Flier, EChaosImpactBallType Rider, const FVector& At, const FVector& Way,
		APawn* Source, AActor* Victim, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, UClass* BallClass, float Reach);
	/** Server: a ノヴァ's blast at At (Scale: its charge's), Source's. */
	CHAOSIMPACT_API void NovaBlast(UWorld* World, const FVector& At, APawn* Source, float Scale, EChaosImpactBallType Tint);
	/** Server: what a fused ノヴァ does while it is charged over its thrower's head (特異点ノヴァ draws in, 守護ノヴァ walls). */
	CHAOSIMPACT_API void TickNovaCharge(AChaosImpactCharacter* Charger, EChaosImpactBallType Rider, float DeltaSeconds, bool bJustStarted);
}

/**
 * A strike that comes after a warning (the crackle's lightning, a storm cloud's, a thundering whirl's): a ring of its
 * colour lights on the floor where it will strike, on every screen; Delay later the server strikes there (a small burst of
 * its kind), and whoever has stepped off is clear.
 */
UCLASS(NotPlaceable)
class AChaosImpactFusionStrike : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactFusionStrike();
	/** Server. */
	static AChaosImpactFusionStrike* Spawn(UWorld* World, EChaosImpactBallType Kind, const FVector& At, float Delay, APawn* Source,
		TSharedPtr<FChaosImpactDuelThrowDamage> Damage, EChaosImpactBallType Tint, float Reach = 0.45f);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	EChaosImpactBallType Kind = EChaosImpactBallType::Thunder;
	UPROPERTY(Replicated)
	float Delay = 0.45f;
	UPROPERTY(Replicated)
	EChaosImpactBallType Tint = EChaosImpactBallType::Normal;
	UPROPERTY(Replicated)
	float Reach = 0.45f;
	TWeakObjectPtr<APawn> Source;
	TSharedPtr<FChaosImpactDuelThrowDamage> Damage;
	float Age = 0.0f;
	bool bStruck = false;
};

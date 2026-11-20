#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"

class AActor;
class AChaosImpactCharacter;
class APawn;
class UNiagaraComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UWorld;
struct FChaosImpactDuelThrowDamage;

/**
 * The fusion ball (合体ボール, prototype since 2026-11-16; see Docs/合体ボール企画.md): with two special balls in hand
 * the 合体 button joins them into one, thrown as one. It flies as the one of the two with the stronger way of flying
 * (ドライブ > ビーム > サンダー > フック > アンカー > ウィンド > スノー > シマエナガ, then the rest) and the other's effect
 * comes with it: where it bursts (and, for some, along the way). Training and an online room's lobby only (never a match).
 */
namespace ChaosImpactFusion
{
	/** The two can be joined: any two specials (2026-11-19); two of the same make a 強化ボール. Never a normal ball. */
	CHAOSIMPACT_API bool CanFuseTypes(EChaosImpactBallType A, EChaosImpactBallType B);
	/** 強化ボール (超〇〇): the two are the same kind. */
	inline bool IsSuper(const EChaosImpactBallType Primary, const EChaosImpactBallType Secondary)
	{
		return Primary == Secondary && Primary != EChaosImpactBallType::Normal;
	}
	/** The one of the two that flies (the other's effect rides along). */
	CHAOSIMPACT_API EChaosImpactBallType PickPrimary(EChaosImpactBallType A, EChaosImpactBallType B);
	inline EChaosImpactBallType PickSecondary(const EChaosImpactBallType A, const EChaosImpactBallType B)
	{
		return PickPrimary(A, B) == A ? B : A;
	}
	/** The pair's name (仮), from the plan: 吸引ドライブ, 炎の竜巻… (logs; the screen shows GetFusionLabel). */
	CHAOSIMPACT_API FString GetFusionName(EChaosImpactBallType A, EChaosImpactBallType B);
	/** As the screen shows it: the one that flies × the one riding along (ドライブ×ファイア). */
	CHAOSIMPACT_API FString GetFusionLabel(EChaosImpactBallType Primary, EChaosImpactBallType Secondary);
	/**
	 * A colour of the flying kind's own look turned toward the second kind's (Amount 0..1), keeping how bright and how
	 * pale it was: a green tornado's strands go flame orange with a fire riding along, its pale core a pale flame.
	 */
	CHAOSIMPACT_API FLinearColor TintToward(const FLinearColor& Base, EChaosImpactBallType Secondary, float Amount);
	/** Every coloured material and light on Owner (its own look, built already) turned toward the second kind's colour. */
	CHAOSIMPACT_API void TintActorLooks(AActor* Owner, EChaosImpactBallType Secondary, float Amount);
	/** How far each look is turned: the flying kind's own (and what it bursts into) far, the one riding along's less. */
	constexpr float TintMain = 0.65f;
	constexpr float TintSide = 0.5f;
	/** A tornado that is the flying kind takes the other's colour almost whole (a fire riding along: a tornado of flame). */
	constexpr float TintTornado = 0.85f;
	/**
	 * Any machine: what Source draws from now on is turned toward Toward's colour (Amount): its soft FX-kit pieces and the
	 * Niagara colours set on its effects. Held weakly; gone with Source.
	 */
	CHAOSIMPACT_API void RegisterTint(const UObject* Source, EChaosImpactBallType Toward, float Amount);
	/** The tint for something drawn for Context (it, its owner, what it is attached to). */
	CHAOSIMPACT_API bool FindTint(const UObject* Context, EChaosImpactBallType& OutToward, float& OutAmount);
	/** Where joining is allowed now: training (not the dojo, not solo), or an online room's lobby (not a match). */
	CHAOSIMPACT_API bool IsAllowedIn(const UWorld* World, const AChaosImpactCharacter* Character);
	/**
	 * (2026-11-19) 1v1 (and tournaments, played by its rules): one throw hurts each one at most once. Everywhere else a
	 * fused ball's parts each hurt: what it struck takes the rider's burst too, a steered piercer hits again while it drags
	 * them, a fused tornado catches again.
	 */
	CHAOSIMPACT_API bool IsOneHitRule(const UWorld* World);
	/** The one a burst leaves out (it took its hit already): only under the one-hit rule. */
	inline AActor* SparedVictim(const UWorld* World, AActor* Victim) { return IsOneHitRule(World) ? Victim : nullptr; }

	/**
	 * Server: the secondary's effect where a fused ball (or the hook / tornado it became) ended. Heading is the way it
	 * was going (a tornado or a beam goes on that way); DirectVictim already took the hit.
	 */
	CHAOSIMPACT_API void ApplySecondary(UWorld* World, EChaosImpactBallType Secondary, const FVector& Location, const FVector& Heading,
		APawn* Source, AActor* DirectVictim, TSharedPtr<FChaosImpactDuelThrowDamage> ThrowDamage, UClass* BallClass,
		EChaosImpactBallType Primary = EChaosImpactBallType::Normal, float Reach = 1.0f);

	/** This machine: the flash where two balls became one (at the hands), in both colours. */
	CHAOSIMPACT_API void PlayJoinFlash(const UObject* WorldContext, const FVector& At, EChaosImpactBallType A, EChaosImpactBallType B);
	/** This machine: a fused ball bursting (over the two effects' own), both colours swirling out. */
	CHAOSIMPACT_API void PlayFusedBurst(const UObject* WorldContext, const FVector& At, EChaosImpactBallType A, EChaosImpactBallType B);
	/** This machine: one soft puff of the fused trail (both colours), every so often along the flight. */
	CHAOSIMPACT_API void PlayTrailPuff(const UObject* WorldContext, const FVector& At, const FVector& Velocity,
		EChaosImpactBallType A, EChaosImpactBallType B, float Size = 1.0f);

	/**
	 * The fused look (ChaosImpactFusionLook.cpp): one ball of the two. Its body is the HUD icon's swirl of the two colours
	 * (put on the ball's own mesh); what marks each kind is round it (flames, ice crystals, crackling arcs, the black ball's
	 * rings, a whirl of wind, smoke, a halo of light, snow, little birds, a claw, an anchor, chasing arrows, a mine's
	 * spikes), so every pair looks like no other; crossed rings and a double helix of motes in the two colours, a light of
	 * both. With a black hole riding along on something flying, its pull is drawn round it (a vortex on the floor, rings
	 * of bent light closing in, streaks drawn in).
	 */
	struct CHAOSIMPACT_API FFusionLook
	{
		struct FSign
		{
			EChaosImpactBallType Type = EChaosImpactBallType::Normal;
			TWeakObjectPtr<USceneComponent> Holder;
			TArray<TWeakObjectPtr<UStaticMeshComponent>> Parts;
			/** Put on the ball itself (snow clumps): taken off with the rest. */
			TArray<TWeakObjectPtr<USceneComponent>> Extras;
			TWeakObjectPtr<UNiagaraComponent> Aura;
			TWeakObjectPtr<class UProceduralMeshComponent> Arcs;
			TWeakObjectPtr<UMaterialInstanceDynamic> Glow;
			ChaosImpactBallTypes::FSimaeBallLook Simae;
			float NextArcAt = 0.0f;
		};
		struct FDressed
		{
			TWeakObjectPtr<UPrimitiveComponent> Mesh;
			TArray<TWeakObjectPtr<UMaterialInterface>> Originals;
		};
		TWeakObjectPtr<USceneComponent> Root;
		TWeakObjectPtr<UMaterialInstanceDynamic> Swirl;
		TArray<FDressed> Dressed;
		TArray<FSign> Signs;
		TWeakObjectPtr<UStaticMeshComponent> Rings[2];
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Motes;
		TWeakObjectPtr<UPointLightComponent> Light;
		TWeakObjectPtr<UStaticMeshComponent> Vortex;
		TWeakObjectPtr<UMaterialInstanceDynamic> VortexMaterial;
		TWeakObjectPtr<UStaticMeshComponent> Lens[2];
		TWeakObjectPtr<UMaterialInstanceDynamic> LensMaterial[2];
		float SuctionRadius = 0.0f;
		float SuctionPuffAt = 0.0f;
		EChaosImpactBallType Primary = EChaosImpactBallType::Normal;
		EChaosImpactBallType Secondary = EChaosImpactBallType::Normal;
		float Scale = 1.0f;
		float Radius = 24.0f;
		bool bAtom = true;
		bool bBuilt = false;
		bool IsBuilt() const { return bBuilt; }
	};
	struct FFusionLookOptions
	{
		/** 1: a thrown ball's size (radius 24). */
		float Scale = 1.0f;
		/** Where its middle is, from what it is put on. */
		FVector Offset = FVector::ZeroVector;
		/** The swirl body on what it is put on (a mesh). */
		bool bBody = true;
		/** Each kind's marks: the flying kind's (a ball in flight has its own look already), the other's. */
		bool bPrimarySign = false;
		bool bSecondarySign = true;
		/** The crossed rings (off: seen from above they read as flat discs over the ball). */
		bool bAtom = false;
		/** Snow packed over the swirl (0..1). */
		float SnowCover = 0.0f;
		/** A black hole's pull drawn round it in flight, this far (0: none). */
		float SuctionRadius = 0.0f;
	};
	/** Builds it on Parent. Replaces whatever was built before. */
	CHAOSIMPACT_API void BuildLook(AActor* Owner, USceneComponent* Parent, EChaosImpactBallType Primary, EChaosImpactBallType Secondary,
		FFusionLook& Look, const FFusionLookOptions& Options);
	/** The swirl body on another mesh too (a rolled snowball); Redress puts it back where something else replaced it. */
	CHAOSIMPACT_API void DressMesh(FFusionLook& Look, UPrimitiveComponent* Mesh);
	CHAOSIMPACT_API void Redress(FFusionLook& Look);
	/** Each frame. Shown false hides it all; Pop (0..1) swells it for a moment (just joined); Flying shows a pull. */
	CHAOSIMPACT_API void UpdateLook(FFusionLook& Look, float Time, float DeltaSeconds, bool bShown, float Pop = 0.0f, bool bFlying = false);
	CHAOSIMPACT_API void DestroyLook(FFusionLook& Look);
	/** This machine: one puff of a pull's streaks of light drawn in to Center from as far as Radius (violet and Other's colour). */
	CHAOSIMPACT_API void PlaySuctionPuff(const UObject* WorldContext, const FVector& Center, float Radius, EChaosImpactBallType Other);
}

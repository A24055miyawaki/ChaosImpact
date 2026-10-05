#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.generated.h"

class AActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UObject;
class UWorld;

/** What a ball does when it lands. Special balls detonate instead of becoming a pickup again. */
UENUM(BlueprintType)
enum class EChaosImpactBallType : uint8
{
	/** Bends a little toward the nearest opponent ahead of it. */
	Normal UMETA(DisplayName="Normal"),
	/** Leaves a line of small fires along its flight; explodes on any contact and leaves burning ground for a few seconds. */
	Fire UMETA(DisplayName="Fire"),
	/** Freezes the ground (slippery) and encases anyone in range at the moment of impact. */
	Ice UMETA(DisplayName="Ice"),
	/** Flies straight at high speed, rebounding off walls and faster after each; bursts into lightning on an opponent or after 3.5 s. */
	Thunder UMETA(DisplayName="Thunder"),
	/** Opens a black hole where it lands that draws opponents to its centre for a few seconds; the very centre burns like fire. */
	Black UMETA(DisplayName="Black"),
	/** Never flies: the throw releases a tornado that weaves forward for a few seconds, hitting and blowing away. */
	Wind UMETA(DisplayName="Wind"),
	/** Bursts into a cloud of smoke: opponents caught in it (or walking in while it lasts) can barely see for a while. */
	Smoke UMETA(DisplayName="Smoke"),
	/** A beam of light: flies dead straight and fast through walls and people alike, hitting every opponent on its line. */
	Beam UMETA(DisplayName="Beam"),
	/** Grows as its carrier walks (so does its hit area): thrown by hand while small, hurled from over the head once big; heavy when big. Shatters on contact. */
	Snow UMETA(DisplayName="Snow"),
	/**
	 * Charged held up over the head, rooted to the spot, while energy streams in from all around and it swells to an
	 * enormous size (a long charge); hurled slowly, it bursts in a blast as wide as it was charged.
	 */
	Nova UMETA(DisplayName="Nova")
};

namespace ChaosImpactBallTypes
{
	constexpr int32 Count = 10;

	/** A thrown thunder ball's speed, however long the throw was charged. */
	constexpr float ThunderSpeed = 4000.0f;
	/** A thunder ball that meets nobody bursts after this long. */
	constexpr float ThunderFlightSeconds = 3.5f;
	/** Each wall a thunder ball rebounds off speeds it up by this much, up to ThunderMaxSpeed. */
	constexpr float ThunderBounceSpeedUp = 1.25f;
	constexpr float ThunderMaxSpeed = 12000.0f;

	/** A normal ball bends toward the nearest opponent ahead of it: within this cone and reach, at this turn rate. */
	constexpr float NormalHomingConeDegrees = 50.0f;
	constexpr float NormalHomingReach = 1600.0f;
	constexpr float NormalHomingDegreesPerSecond = 55.0f;

	/** Smoke: the cloud's reach and how long it hangs; how long someone caught at the burst, or inside the cloud, stays blinded. */
	constexpr float SmokeRadius = 400.0f;
	constexpr float SmokeCloudSeconds = 3.5f;
	constexpr float SmokeBlindSeconds = 4.0f;
	constexpr float SmokeLingerBlindSeconds = 2.5f;

	/** Beam: its speed (however the throw was charged), how far it goes, and how close to its line counts as a hit. */
	constexpr float BeamSpeed = 5200.0f;
	constexpr float BeamRange = 9000.0f;
	constexpr float BeamHitRadius = 70.0f;
	/** Height difference from the beam that still counts (it is drawn as a thick shaft of light). */
	constexpr float BeamHitHeight = 150.0f;

	/** Snow: its size (to a normal ball's) when picked up and at its biggest, reached after this much walking. */
	constexpr float SnowMinScale = 0.6f;
	constexpr float SnowMaxScale = 6.0f;
	constexpr float SnowGrowDistance = 3600.0f;
	/**
	 * Up to this size a snowball is carried in the hand and thrown like any ball; bigger, it is held up over the
	 * head and hurled from there in a falling arc (this fast upward at release).
	 */
	constexpr float SnowOverheadScale = 1.6f;
	constexpr float SnowThrowUpSpeed = 250.0f;
	inline bool IsSnowOverhead(const float Scale) { return Scale >= SnowOverheadScale; }
	/** Walking speed lost carrying a snowball at its biggest (a small one costs nothing). */
	constexpr float SnowMaxSlowdown = 0.2f;
	inline float GetSnowScale(const float Growth)
	{
		return FMath::Lerp(SnowMinScale, SnowMaxScale, FMath::Clamp(Growth, 0.0f, 1.0f));
	}

	/**
	 * Nova: a full charge takes this long, during which its thrower cannot move. Its size (to a normal ball's) grows
	 * from the first to the second over the charge, faster toward the end.
	 */
	constexpr float NovaChargeSeconds = 4.0f;
	constexpr float NovaMinScale = 1.2f;
	constexpr float NovaMaxScale = 22.0f;
	/** Hurled slowly from over the head, a little upward, falling gently (a fraction of normal gravity). */
	constexpr float NovaThrowSpeed = 1250.0f;
	constexpr float NovaThrowUpSpeed = 220.0f;
	constexpr float NovaGravityScale = 0.45f;
	/** Gap between the top of the thrower's head and the bottom of the ball held up. */
	constexpr float NovaHoldGap = 35.0f;
	inline float GetNovaScale(const float Charge)
	{
		return FMath::Lerp(NovaMinScale, NovaMaxScale, FMath::Pow(FMath::Clamp(Charge, 0.0f, 1.0f), 1.6f));
	}
	/** How far the blast reaches for a ball this big (about 12 m at its biggest). */
	inline float GetNovaBlastRadius(const float Scale) { return 180.0f + 46.0f * Scale; }
	/** Nearly fully charged, its blast hits twice as hard. */
	inline float GetNovaDamage(const float Scale) { return Scale >= NovaMaxScale * 0.9f ? 2.0f : 1.0f; }

	/** A fire ball's flight leaves a line of small fires on the ground, this far apart, at most this many. */
	constexpr float FireTrailSpacing = 130.0f;
	constexpr int32 FireTrailMaxPatches = 26;

	inline EChaosImpactBallType FromIndex(const int32 Index)
	{
		return static_cast<EChaosImpactBallType>(FMath::Clamp(Index, 0, Count - 1));
	}

	inline FLinearColor GetColor(const EChaosImpactBallType Type)
	{
		switch (Type)
		{
		case EChaosImpactBallType::Fire: return FLinearColor(1.0f, 0.34f, 0.02f, 1.0f);
		case EChaosImpactBallType::Ice: return FLinearColor(0.42f, 0.86f, 1.0f, 1.0f);
		case EChaosImpactBallType::Thunder: return FLinearColor(1.0f, 0.88f, 0.22f, 1.0f);
		case EChaosImpactBallType::Black: return FLinearColor(0.62f, 0.24f, 1.0f, 1.0f);
		case EChaosImpactBallType::Wind: return FLinearColor(0.35f, 1.0f, 0.45f, 1.0f);
		case EChaosImpactBallType::Smoke: return FLinearColor(0.72f, 0.7f, 0.82f, 1.0f);
		case EChaosImpactBallType::Beam: return FLinearColor(1.0f, 0.24f, 0.78f, 1.0f);
		case EChaosImpactBallType::Snow: return FLinearColor(0.9f, 0.96f, 1.0f, 1.0f);
		case EChaosImpactBallType::Nova: return FLinearColor(0.45f, 0.86f, 1.0f, 1.0f);
		default: return FLinearColor(0.0f, 0.82f, 1.0f, 1.0f);
		}
	}

	inline const TCHAR* GetDisplayName(const EChaosImpactBallType Type)
	{
		switch (Type)
		{
		case EChaosImpactBallType::Fire: return TEXT("ファイア");
		case EChaosImpactBallType::Ice: return TEXT("アイス");
		case EChaosImpactBallType::Thunder: return TEXT("サンダー");
		case EChaosImpactBallType::Black: return TEXT("ブラック");
		case EChaosImpactBallType::Wind: return TEXT("ウィンド");
		case EChaosImpactBallType::Smoke: return TEXT("スモーク");
		case EChaosImpactBallType::Beam: return TEXT("ビーム");
		case EChaosImpactBallType::Snow: return TEXT("スノー");
		case EChaosImpactBallType::Nova: return TEXT("ノヴァ");
		default: return TEXT("ノーマル");
		}
	}

	/** For logs. */
	inline const TCHAR* GetInternalName(const EChaosImpactBallType Type)
	{
		switch (Type)
		{
		case EChaosImpactBallType::Fire: return TEXT("Fire");
		case EChaosImpactBallType::Ice: return TEXT("Ice");
		case EChaosImpactBallType::Thunder: return TEXT("Thunder");
		case EChaosImpactBallType::Black: return TEXT("Black");
		case EChaosImpactBallType::Wind: return TEXT("Wind");
		case EChaosImpactBallType::Smoke: return TEXT("Smoke");
		case EChaosImpactBallType::Beam: return TEXT("Beam");
		case EChaosImpactBallType::Snow: return TEXT("Snow");
		case EChaosImpactBallType::Nova: return TEXT("Nova");
		default: return TEXT("Normal");
		}
	}

	/**
	 * Carried inventory is packed four bits per slot (up to 16 types), slot 0 first (the right hand, thrown next); a
	 * player carries at most two balls, so both fit one byte. Small enough to send with every ordered pickup/throw answer.
	 */
	constexpr int32 PackedBitsPerSlot = 4;
	constexpr int32 PackedSlotMask = 0xF;
	constexpr int32 MaxPackedSlots = 2;

	inline EChaosImpactBallType GetPackedSlot(const uint8 Packed, const int32 Slot)
	{
		return FromIndex((Packed >> (Slot * PackedBitsPerSlot)) & PackedSlotMask);
	}

	inline uint8 SetPackedSlot(const uint8 Packed, const int32 Slot, const EChaosImpactBallType Type)
	{
		const int32 Shift = FMath::Clamp(Slot, 0, MaxPackedSlots - 1) * PackedBitsPerSlot;
		return static_cast<uint8>((Packed & ~(PackedSlotMask << Shift)) | (static_cast<int32>(Type) << Shift));
	}

	inline uint8 Pack(const TConstArrayView<EChaosImpactBallType> Types)
	{
		uint8 Packed = 0;
		for (int32 Slot = 0; Slot < FMath::Min(Types.Num(), MaxPackedSlots); ++Slot)
		{
			Packed = SetPackedSlot(Packed, Slot, Types[Slot]);
		}
		return Packed;
	}

	/** Runtime FX materials (Content/ChaosImpact/FX); fall back to template content when missing. */
	CHAOSIMPACT_API UMaterialInterface* GetAdditiveMaterial();
	CHAOSIMPACT_API UMaterialInterface* GetEmissiveMaterial();
	CHAOSIMPACT_API UMaterialInterface* GetIceMaterial();
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeAdditive(UObject* Outer, const FLinearColor& Color, float Intensity, float RimOnly = 0.0f);
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeEmissive(UObject* Outer, const FLinearColor& Color, float Intensity);
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeIce(UObject* Outer, const FLinearColor& Color, float Opacity, float Intensity = 1.4f);
	/** Lit, glossy, see-through ice for faceted crystal meshes. */
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeIceCrystal(UObject* Outer, float Opacity, float Glow = 0.18f,
		const FLinearColor& Tint = FLinearColor(0.14f, 0.42f, 0.72f));
	/** Frozen ground with frost, cracks and a ragged edge, for the engine plane mesh. */
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeIceSurface(UObject* Outer);
	/** Packed snow: lit, matte, faintly blue in the shade. */
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeSnow(UObject* Outer);
	/**
	 * Clumps of snow stuck on a snowball, as children of Parent (an engine sphere sized like the ball), so it reads
	 * as rolled snow rather than a perfect ball. They scale and turn with it.
	 */
	CHAOSIMPACT_API void AttachSnowLumps(UObject* Owner, USceneComponent* Parent, UMaterialInterface* Look, int32 Seed);
	/** A snowball breaking up: white chunks and a puff of powder. */
	CHAOSIMPACT_API void PlaySnowBurst(UObject* WorldContext, const FVector& Location, float Scale);
	/** A burst of the beam's pink light, where it strikes someone or fades out. */
	CHAOSIMPACT_API void PlayBeamBurst(UObject* WorldContext, const FVector& Location, float Scale);

	/**
	 * How a nova looks, held up or flying: a white-hot core inside layers of blue light with bands of energy turning
	 * round it, sparks and its own light. Built on a parent it follows; UpdateNovaLook sizes it every frame.
	 */
	struct FNovaLook
	{
		TWeakObjectPtr<UStaticMeshComponent> Core;
		TWeakObjectPtr<UStaticMeshComponent> Inner;
		TWeakObjectPtr<UStaticMeshComponent> Shell;
		TWeakObjectPtr<UStaticMeshComponent> Halo;
		TArray<TWeakObjectPtr<UStaticMeshComponent>, TInlineAllocator<3>> Bands;
		TWeakObjectPtr<UMaterialInstanceDynamic> InnerMaterial;
		TWeakObjectPtr<UMaterialInstanceDynamic> ShellMaterial;
		TWeakObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
		TWeakObjectPtr<UPointLightComponent> Light;
		TWeakObjectPtr<UNiagaraComponent> Sparks;
		bool IsBuilt() const { return Core.IsValid(); }
	};
	CHAOSIMPACT_API void BuildNovaLook(AActor* Owner, USceneComponent* Parent, FNovaLook& Out);
	/** Radius in centimetres; Glow scales how bright it all is. */
	CHAOSIMPACT_API void UpdateNovaLook(const FNovaLook& Look, float Radius, float Time, float Glow = 1.0f);
	CHAOSIMPACT_API void SetNovaLookVisible(const FNovaLook& Look, bool bVisible);

	/** Systems from the Niagara Examples Pack (Content/NiagaraExamples). */
	namespace Effects
	{
		inline const TCHAR* Explosion = TEXT("/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Small.NS_Explosion_Small");
		inline const TCHAR* Fire = TEXT("/Game/NiagaraExamples/FX_Misc/NS_Fire.NS_Fire");
		inline const TCHAR* Smoke = TEXT("/Game/NiagaraExamples/FX_Smoke/NS_Smoke_Plume.NS_Smoke_Plume");
		inline const TCHAR* Shatter = TEXT("/Game/NiagaraExamples/FX_Weapons/Impacts/NS_Impact_Glass.NS_Impact_Glass");
		inline const TCHAR* FireTrail = TEXT("/Game/NiagaraExamples/FX_Weapons/Trails/NS_RocketTrail.NS_RocketTrail");
		inline const TCHAR* BallTrail = TEXT("/Game/NiagaraExamples/FX_Weapons/Trails/NS_SimpleRibbonTrail.NS_SimpleRibbonTrail");
		inline const TCHAR* Damage = TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage");
		// Thunder and black balls.
		inline const TCHAR* Electricity = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Electricity_Looping.NS_Player_Electricity_Looping");
		inline const TCHAR* SparkBurst = TEXT("/Game/NiagaraExamples/FX_Sparks/NS_Spark_Burst.NS_Spark_Burst");
		inline const TCHAR* DarkAura = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_DeBuff_Looping.NS_Player_DeBuff_Looping");
		// Wind ball tornado.
		inline const TCHAR* WindSparks = TEXT("/Game/NiagaraExamples/FX_Sparks/NS_Spark_Continuous.NS_Spark_Continuous");
		inline const TCHAR* DirtBurstSmall = TEXT("/Game/NiagaraExamples/FX_Explosions/NS_Dirt_Explosion_Small.NS_Dirt_Explosion_Small");
		inline const TCHAR* DirtBurstMedium = TEXT("/Game/NiagaraExamples/FX_Explosions/NS_Dirt_Explosion_Medium.NS_Dirt_Explosion_Medium");
		// Warp pads.
		inline const TCHAR* WarpAura = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Buff_Looping.NS_Player_Buff_Looping");
		inline const TCHAR* WarpOut = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Teleport_Out.NS_Player_Teleport_Out");
		inline const TCHAR* WarpIn = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Teleport_In.NS_Player_Teleport_In");
		// A character on its last hit; the smoke ball's wisps too.
		inline const TCHAR* LastHitSmoke = TEXT("/Game/NiagaraExamples/FX_Smoke/NS_Chimney_Smoke.NS_Chimney_Smoke");
		// Beam: a flash as it leaves the hand.
		inline const TCHAR* MuzzleFlash = TEXT("/Game/NiagaraExamples/FX_Weapons/MuzzleFlashes/NS_MuzzleFlash.NS_MuzzleFlash");
		// Nova: its great blast.
		inline const TCHAR* BigExplosion = TEXT("/Game/NiagaraExamples/FX_Explosions/NS_Explosion.NS_Explosion");
		inline const TCHAR* MediumExplosion = TEXT("/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Medium.NS_Explosion_Medium");
		inline const TCHAR* DirtBurstLarge = TEXT("/Game/NiagaraExamples/FX_Explosions/NS_Dirt_Explosion.NS_Dirt_Explosion");
	}
	CHAOSIMPACT_API UNiagaraSystem* LoadEffect(const TCHAR* ObjectPath);
	/** Loads every FX system and material up front (game start); the caller keeps the objects alive. */
	CHAOSIMPACT_API void PreloadAssets(TArray<TObjectPtr<UObject>>& OutKeepAlive);
	/**
	 * Shows each FX system and material once, out of sight, when a play world starts, so the first-use
	 * costs (component and PSO creation, GPU buffers) do not land on the first throw.
	 */
	CHAOSIMPACT_API void WarmUpEffects(UWorld* World, const FVector& Location);
	/** Sets a user parameter by its display name; the pack also exposes a space-less spelling, set too. */
	CHAOSIMPACT_API void SetEffectColor(UNiagaraComponent* Effect, const TCHAR* Name, const FLinearColor& Color);
	CHAOSIMPACT_API void SetEffectFloat(UNiagaraComponent* Effect, const TCHAR* Name, float Value);
	CHAOSIMPACT_API void SetEffectSize(UNiagaraComponent* Effect, const TCHAR* Name, float Value);
	CHAOSIMPACT_API void SetEffectVector(UNiagaraComponent* Effect, const TCHAR* Name, const FVector& Value);
	/** Ice breaking apart: glass-impact shards tinted like ice, with a puff of cold mist. */
	CHAOSIMPACT_API void PlayIceShatter(UObject* WorldContext, const FVector& Location, float Scale, float Amount);
}

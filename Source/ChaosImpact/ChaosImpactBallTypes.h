#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UObject;
class UWorld;

/** What a ball does when it lands. Special balls detonate instead of becoming a pickup again. */
UENUM(BlueprintType)
enum class EChaosImpactBallType : uint8
{
	Normal UMETA(DisplayName="Normal"),
	/** Explodes on any contact and leaves burning ground for a few seconds. */
	Fire UMETA(DisplayName="Fire"),
	/** Freezes the ground (slippery) and encases anyone in range at the moment of impact. */
	Ice UMETA(DisplayName="Ice"),
	/** Flies straight at a fixed high speed, rebounding off walls; bursts into lightning on an opponent or after 3 s. */
	Thunder UMETA(DisplayName="Thunder"),
	/** Opens a black hole where it lands that draws opponents to its centre for a few seconds (no damage). */
	Black UMETA(DisplayName="Black")
};

namespace ChaosImpactBallTypes
{
	constexpr int32 Count = 5;

	/** A thrown thunder ball's speed, however long the throw was charged. */
	constexpr float ThunderSpeed = 4000.0f;
	/** A thunder ball that meets nobody bursts after this long. */
	constexpr float ThunderFlightSeconds = 3.0f;

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
		default: return TEXT("Normal");
		}
	}

	/**
	 * Carried inventory is packed three bits per slot, slot 0 first (the right hand, thrown next); a player
	 * carries at most two balls. Small enough to send with every ordered pickup/throw answer.
	 */
	constexpr int32 PackedBitsPerSlot = 3;
	constexpr int32 PackedSlotMask = 0x7;
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
		// Warp pads.
		inline const TCHAR* WarpAura = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Buff_Looping.NS_Player_Buff_Looping");
		inline const TCHAR* WarpOut = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Teleport_Out.NS_Player_Teleport_Out");
		inline const TCHAR* WarpIn = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Teleport_In.NS_Player_Teleport_In");
		// A character on its last hit.
		inline const TCHAR* LastHitSmoke = TEXT("/Game/NiagaraExamples/FX_Smoke/NS_Chimney_Smoke.NS_Chimney_Smoke");
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

#include "ChaosImpactSfx.h"

#include "ChaosImpact.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	/** How far a sound carries. */
	enum class ESfxReach : uint8 { Screen, Near, Mid, Far };

	struct FSfxSpec
	{
		/** SFX_<Name>_NN. */
		const TCHAR* Name;
		uint8 Variants;
		float Volume;
		/** Each play's pitch is 1 +- this, so repeats do not sound copied. */
		float PitchJitter;
		ESfxReach Reach;
		/** It does not start again within this (seconds): no stacking when many happen at once. */
		float MinInterval;
	};

	// In the order of EChaosImpactSfx. Volumes balance on top of the levels the sounds were made at.
	constexpr FSfxSpec Specs[] =
	{
		{TEXT("UI_Move"), 3, 0.9f, 0.03f, ESfxReach::Screen, 0.03f},
		{TEXT("UI_Confirm"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.05f},
		{TEXT("UI_Back"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.05f},
		{TEXT("UI_Deny"), 1, 0.9f, 0.0f, ESfxReach::Screen, 0.12f},
		{TEXT("UI_Tab"), 1, 1.0f, 0.03f, ESfxReach::Screen, 0.05f},
		{TEXT("UI_Value"), 2, 0.9f, 0.02f, ESfxReach::Screen, 0.03f},
		{TEXT("UI_Open"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.1f},
		{TEXT("UI_Type"), 3, 0.9f, 0.04f, ESfxReach::Screen, 0.02f},
		{TEXT("UI_Erase"), 1, 0.9f, 0.03f, ESfxReach::Screen, 0.03f},
		{TEXT("UI_Start"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.3f},
		{TEXT("UI_Select"), 1, 0.9f, 0.0f, ESfxReach::Screen, 0.1f},
		{TEXT("UI_Ready"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.1f},
		{TEXT("UI_Reveal"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.5f},
		{TEXT("Match_Tick"), 1, 0.9f, 0.0f, ESfxReach::Screen, 0.3f},
		{TEXT("Match_Go"), 1, 1.0f, 0.0f, ESfxReach::Screen, 1.0f},
		{TEXT("Match_Ready"), 1, 1.0f, 0.0f, ESfxReach::Screen, 1.0f},
		{TEXT("Match_Starting"), 1, 0.9f, 0.0f, ESfxReach::Screen, 1.0f},
		{TEXT("Match_Minute"), 1, 0.9f, 0.0f, ESfxReach::Screen, 2.0f},
		{TEXT("Match_Finish"), 1, 1.0f, 0.0f, ESfxReach::Screen, 2.0f},
		{TEXT("Match_Cheer"), 1, 0.8f, 0.0f, ESfxReach::Screen, 2.0f},
		{TEXT("Match_Confetti"), 1, 0.9f, 0.0f, ESfxReach::Screen, 0.5f},
		{TEXT("Match_KO"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.2f},
		{TEXT("Char_Jump"), 2, 0.9f, 0.04f, ESfxReach::Near, 0.05f},
		{TEXT("Char_Land"), 3, 0.9f, 0.04f, ESfxReach::Near, 0.06f},
		{TEXT("Char_Dash"), 2, 1.0f, 0.04f, ESfxReach::Mid, 0.04f},
		{TEXT("Char_Pickup"), 2, 1.0f, 0.04f, ESfxReach::Near, 0.03f},
		{TEXT("Char_Throw"), 3, 1.0f, 0.04f, ESfxReach::Mid, 0.03f},
		{TEXT("Char_ThrowHeavy"), 1, 1.0f, 0.03f, ESfxReach::Mid, 0.05f},
		{TEXT("Char_ChargeReady"), 1, 0.8f, 0.0f, ESfxReach::Screen, 0.1f},
		{TEXT("Char_Hit"), 3, 1.0f, 0.04f, ESfxReach::Mid, 0.03f},
		{TEXT("Char_KO"), 1, 1.0f, 0.03f, ESfxReach::Far, 0.05f},
		{TEXT("Char_Respawn"), 1, 0.9f, 0.0f, ESfxReach::Mid, 0.05f},
		{TEXT("Char_Freeze"), 1, 1.0f, 0.03f, ESfxReach::Mid, 0.05f},
		{TEXT("Char_IceShatter"), 2, 1.0f, 0.04f, ESfxReach::Mid, 0.05f},
		{TEXT("Char_Swap"), 1, 0.9f, 0.04f, ESfxReach::Near, 0.05f},
		{TEXT("Ball_Bounce"), 3, 0.9f, 0.06f, ESfxReach::Near, 0.04f},
		{TEXT("Ball_Hit"), 2, 1.0f, 0.04f, ESfxReach::Mid, 0.03f},
		{TEXT("Fire_Throw"), 1, 1.0f, 0.04f, ESfxReach::Mid, 0.05f},
		{TEXT("Fire_Loop"), 1, 0.8f, 0.05f, ESfxReach::Near, 0.0f},
		{TEXT("Fire_Explode"), 1, 1.0f, 0.04f, ESfxReach::Far, 0.05f},
		{TEXT("Fire_Sizzle"), 1, 0.8f, 0.06f, ESfxReach::Near, 0.15f},
		{TEXT("Ice_Burst"), 1, 1.0f, 0.04f, ESfxReach::Far, 0.05f},
		{TEXT("Thunder_Loop"), 1, 0.7f, 0.04f, ESfxReach::Near, 0.0f},
		{TEXT("Thunder_Bounce"), 2, 0.9f, 0.05f, ESfxReach::Mid, 0.04f},
		{TEXT("Thunder_Burst"), 1, 1.0f, 0.03f, ESfxReach::Far, 0.05f},
		{TEXT("Thunder_Shock"), 2, 0.9f, 0.05f, ESfxReach::Mid, 0.08f},
		{TEXT("Black_Open"), 1, 1.0f, 0.02f, ESfxReach::Far, 0.05f},
		{TEXT("Black_Close"), 1, 0.9f, 0.02f, ESfxReach::Mid, 0.05f},
		{TEXT("Wind_Tornado"), 1, 1.0f, 0.03f, ESfxReach::Far, 0.05f},
		{TEXT("Wind_Loop"), 1, 0.8f, 0.04f, ESfxReach::Mid, 0.0f},
		{TEXT("Wind_Gust"), 2, 0.9f, 0.05f, ESfxReach::Mid, 0.08f},
		{TEXT("Smoke_Poof"), 1, 1.0f, 0.04f, ESfxReach::Far, 0.05f},
		{TEXT("Beam_Fire"), 1, 1.0f, 0.03f, ESfxReach::Far, 0.05f},
		{TEXT("Beam_Strike"), 1, 1.0f, 0.05f, ESfxReach::Mid, 0.05f},
		{TEXT("Snow_Burst"), 2, 1.0f, 0.05f, ESfxReach::Mid, 0.05f},
		{TEXT("Nova_Charge"), 1, 0.9f, 0.0f, ESfxReach::Mid, 0.0f},
		{TEXT("Nova_Throw"), 1, 1.0f, 0.02f, ESfxReach::Far, 0.05f},
		{TEXT("Nova_Blast"), 1, 1.0f, 0.02f, ESfxReach::Far, 0.1f},
		{TEXT("Simae_Flock"), 1, 1.0f, 0.04f, ESfxReach::Mid, 0.1f},
		{TEXT("Simae_Peck"), 2, 0.9f, 0.08f, ESfxReach::Near, 0.05f},
		{TEXT("Drive_Loop"), 1, 0.7f, 0.0f, ESfxReach::Mid, 0.0f},
		{TEXT("Drive_Burst"), 1, 1.0f, 0.03f, ESfxReach::Far, 0.05f},
		{TEXT("Stage_Warp"), 1, 0.9f, 0.03f, ESfxReach::Mid, 0.1f},
		{TEXT("Stage_BallSpawn"), 1, 0.8f, 0.06f, ESfxReach::Near, 0.08f},
		{TEXT("Stage_TargetHit"), 1, 1.0f, 0.04f, ESfxReach::Mid, 0.04f},
		{TEXT("Solo_DoorClose"), 1, 1.0f, 0.0f, ESfxReach::Far, 0.3f},
		{TEXT("Solo_DoorOpen"), 1, 1.0f, 0.0f, ESfxReach::Far, 0.3f},
		{TEXT("Solo_Boss"), 1, 1.0f, 0.0f, ESfxReach::Screen, 1.0f},
		{TEXT("Solo_Clear"), 1, 1.0f, 0.0f, ESfxReach::Screen, 1.0f},
		{TEXT("Mini_Tap"), 2, 0.9f, 0.05f, ESfxReach::Screen, 0.04f},
		{TEXT("Mini_Score"), 1, 0.9f, 0.04f, ESfxReach::Screen, 0.05f},
		{TEXT("Mini_Big"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.15f},
		{TEXT("Mini_Miss"), 1, 1.0f, 0.0f, ESfxReach::Screen, 0.3f},
	};
	static_assert(UE_ARRAY_COUNT(Specs) == static_cast<int32>(EChaosImpactSfx::Count), "One entry per sound, in order");

	const FSfxSpec& SpecOf(const EChaosImpactSfx Sound)
	{
		return Specs[FMath::Clamp(static_cast<int32>(Sound), 0, static_cast<int32>(EChaosImpactSfx::Count) - 1)];
	}

	double LastPlayedAt[static_cast<int32>(EChaosImpactSfx::Count)] = {};
	int32 PlayCounts[static_cast<int32>(EChaosImpactSfx::Count)] = {};
	int32 LastVariant[static_cast<int32>(EChaosImpactSfx::Count)] = {};
	TWeakObjectPtr<USoundBase> Loaded[static_cast<int32>(EChaosImpactSfx::Count)][4];

	bool IsTracing()
	{
		static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("CISfxTrace"));
		return bTrace;
	}

	UWorld* ResolveWorld(const UObject* WorldContext)
	{
		UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World && GEngine && GEngine->GameViewport)
		{
			World = GEngine->GameViewport->GetWorld();
		}
		return World && World->GetNetMode() != NM_DedicatedServer && !IsRunningCommandlet() ? World : nullptr;
	}

	/** Too soon after the last one (and otherwise counts this one), then a variant (not the same twice running). */
	USoundBase* Take(const EChaosImpactSfx Sound)
	{
		const int32 Index = static_cast<int32>(Sound);
		const FSfxSpec& Spec = SpecOf(Sound);
		const double Now = FPlatformTime::Seconds();
		if (Spec.MinInterval > 0.0f && Now - LastPlayedAt[Index] < Spec.MinInterval)
		{
			return nullptr;
		}
		int32 Variant = 0;
		if (Spec.Variants > 1)
		{
			Variant = FMath::RandRange(0, Spec.Variants - 2);
			Variant += Variant >= LastVariant[Index] ? 1 : 0;
		}
		USoundBase* Asset = Loaded[Index][Variant].Get();
		if (!Asset)
		{
			Asset = LoadObject<USoundBase>(nullptr, *ChaosImpactSfx::GetAssetPath(Sound, Variant));
			Loaded[Index][Variant] = Asset;
		}
		if (!Asset)
		{
			return nullptr;
		}
		LastPlayedAt[Index] = Now;
		LastVariant[Index] = Variant;
		++PlayCounts[Index];
		if (IsTracing())
		{
			UE_LOG(LogChaosImpact, Display, TEXT("SFX %s_%02d"), Spec.Name, Variant + 1);
		}
		return Asset;
	}

	float PitchFor(const EChaosImpactSfx Sound, const float Pitch)
	{
		const float Jitter = SpecOf(Sound).PitchJitter;
		return Pitch * (Jitter > 0.0f ? FMath::FRandRange(1.0f - Jitter, 1.0f + Jitter) : 1.0f);
	}

	/** One shared attenuation per reach: full volume up close, gone by the far edge. */
	USoundAttenuation* AttenuationFor(const ESfxReach Reach)
	{
		static USoundAttenuation* Made[4] = {};
		const int32 Index = static_cast<int32>(Reach);
		if (!Made[Index])
		{
			const float Falloff[] = {0.0f, 2400.0f, 3600.0f, 6500.0f};
			USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage());
			Attenuation->AddToRoot();
			Attenuation->Attenuation.bAttenuate = true;
			Attenuation->Attenuation.bSpatialize = true;
			Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
			Attenuation->Attenuation.AttenuationShapeExtents = FVector(Falloff[Index] * 0.22f, 0.0f, 0.0f);
			Attenuation->Attenuation.FalloffDistance = Falloff[Index];
			// Never fully hard-panned: the camera sits well back from the action.
			Attenuation->Attenuation.NonSpatializedRadiusStart = 500.0f;
			Attenuation->Attenuation.NonSpatializedRadiusEnd = 150.0f;
			Made[Index] = Attenuation;
		}
		return Made[Index];
	}
}

void ChaosImpactSfx::Play2D(const UObject* WorldContext, const EChaosImpactSfx Sound, const float Volume, const float Pitch)
{
	UWorld* World = ResolveWorld(WorldContext);
	USoundBase* Asset = World ? Take(Sound) : nullptr;
	if (Asset)
	{
		UGameplayStatics::PlaySound2D(World, Asset, Volume * SpecOf(Sound).Volume, PitchFor(Sound, Pitch), 0.0f, nullptr, nullptr, true);
	}
}

void ChaosImpactSfx::PlayAt(const UObject* WorldContext, const EChaosImpactSfx Sound, const FVector& Location, const float Volume,
	const float Pitch)
{
	UWorld* World = ResolveWorld(WorldContext);
	USoundBase* Asset = World ? Take(Sound) : nullptr;
	if (!Asset)
	{
		return;
	}
	const FSfxSpec& Spec = SpecOf(Sound);
	if (Spec.Reach == ESfxReach::Screen)
	{
		UGameplayStatics::PlaySound2D(World, Asset, Volume * Spec.Volume, PitchFor(Sound, Pitch));
		return;
	}
	UGameplayStatics::PlaySoundAtLocation(World, Asset, Location, FRotator::ZeroRotator, Volume * Spec.Volume, PitchFor(Sound, Pitch),
		0.0f, AttenuationFor(Spec.Reach));
}

UAudioComponent* ChaosImpactSfx::PlayAttached(const EChaosImpactSfx Sound, USceneComponent* AttachTo, const float Volume,
	const float Pitch)
{
	if (!AttachTo || !ResolveWorld(AttachTo))
	{
		return nullptr;
	}
	USoundBase* Asset = Take(Sound);
	if (!Asset)
	{
		return nullptr;
	}
	const FSfxSpec& Spec = SpecOf(Sound);
	return UGameplayStatics::SpawnSoundAttached(Asset, AttachTo, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		true, Volume * Spec.Volume, PitchFor(Sound, Pitch), 0.0f, Spec.Reach == ESfxReach::Screen ? nullptr : AttenuationFor(Spec.Reach),
		nullptr, true);
}

void ChaosImpactSfx::Stop(UAudioComponent* Playing, const float FadeSeconds)
{
	if (IsValid(Playing) && Playing->IsPlaying())
	{
		Playing->FadeOut(FadeSeconds, 0.0f);
	}
}

const TCHAR* ChaosImpactSfx::GetName(const EChaosImpactSfx Sound)
{
	return SpecOf(Sound).Name;
}

int32 ChaosImpactSfx::GetVariantCount(const EChaosImpactSfx Sound)
{
	return SpecOf(Sound).Variants;
}

FString ChaosImpactSfx::GetAssetPath(const EChaosImpactSfx Sound, const int32 Variant)
{
	const FString Asset = FString::Printf(TEXT("SFX_%s_%02d"), SpecOf(Sound).Name, Variant + 1);
	return FString::Printf(TEXT("/Game/ChaosImpact/Audio/SFX/%s.%s"), *Asset, *Asset);
}

void ChaosImpactSfx::GetAllAssetPaths(TArray<FSoftObjectPath>& OutPaths)
{
	for (int32 Index = 0; Index < static_cast<int32>(EChaosImpactSfx::Count); ++Index)
	{
		for (int32 Variant = 0; Variant < Specs[Index].Variants; ++Variant)
		{
			OutPaths.Add(FSoftObjectPath(GetAssetPath(static_cast<EChaosImpactSfx>(Index), Variant)));
		}
	}
}

int32 ChaosImpactSfx::GetPlayCount(const EChaosImpactSfx Sound)
{
	return PlayCounts[FMath::Clamp(static_cast<int32>(Sound), 0, static_cast<int32>(EChaosImpactSfx::Count) - 1)];
}

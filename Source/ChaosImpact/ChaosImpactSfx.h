#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USceneComponent;
class USoundBase;

/**
 * Every sound effect the game plays. Each is one or more Sound Waves in /Game/ChaosImpact/Audio/SFX named
 * SFX_<Name>_01, SFX_<Name>_02, ... (one picked at random each time); the table in ChaosImpactSfx.cpp gives each its
 * name, how many there are, its volume, pitch wobble, how far it carries (or none: not in the world) and how often it
 * may repeat.
 */
enum class EChaosImpactSfx : uint8
{
	// Menus
	UiMove, UiConfirm, UiBack, UiDeny, UiTab, UiValue, UiOpen, UiType, UiErase, UiStart, UiSelect, UiReady, UiReveal,
	// The match: countdown, start, the last minute, the finish, the results
	MatchTick, MatchGo, MatchReady, MatchStarting, MatchMinute, MatchFinish, MatchCheer, MatchConfetti, MatchKO,
	// The characters
	CharJump, CharLand, CharDash, CharPickup, CharThrow, CharThrowHeavy, CharChargeReady, CharHit, CharKO, CharRespawn,
	CharFreeze, CharIceShatter, CharSwap,
	// The balls
	BallBounce, BallHit, FireThrow, FireLoop, FireExplode, FireSizzle, IceBurst, ThunderLoop, ThunderBounce, ThunderBurst,
	ThunderShock, BlackOpen, BlackClose, WindTornado, WindLoop, WindGust, SmokePoof, BeamFire, BeamStrike, SnowBurst,
	NovaCharge, NovaThrow, NovaBlast, SimaeFlock, SimaePeck, DriveLoop, DriveBurst,
	// The stage
	StageWarp, StageBallSpawn, StageTargetHit, SoloDoorClose, SoloDoorOpen, SoloBoss, SoloClear,
	// The loading screen's little games
	MiniTap, MiniScore, MiniBig, MiniMiss,
	Count
};

namespace ChaosImpactSfx
{
	/** Not placed in the world (menus, announcements, the loading screen's games); plays while paused too. */
	CHAOSIMPACT_API void Play2D(const UObject* WorldContext, EChaosImpactSfx Sound, float Volume = 1.0f, float Pitch = 1.0f);
	/** At a place in the world, quieter with distance. */
	CHAOSIMPACT_API void PlayAt(const UObject* WorldContext, EChaosImpactSfx Sound, const FVector& Location, float Volume = 1.0f,
		float Pitch = 1.0f);
	/** Follows a component (loops keep going until Stop, or until the component goes). */
	CHAOSIMPACT_API UAudioComponent* PlayAttached(EChaosImpactSfx Sound, USceneComponent* AttachTo, float Volume = 1.0f,
		float Pitch = 1.0f);
	/** Fades out and ends a sound from PlayAttached (null is fine). */
	CHAOSIMPACT_API void Stop(UAudioComponent* Playing, float FadeSeconds = 0.25f);

	CHAOSIMPACT_API const TCHAR* GetName(EChaosImpactSfx Sound);
	CHAOSIMPACT_API int32 GetVariantCount(EChaosImpactSfx Sound);
	/** /Game/ChaosImpact/Audio/SFX/SFX_<Name>_<NN>.SFX_<Name>_<NN> (Variant from 0). */
	CHAOSIMPACT_API FString GetAssetPath(EChaosImpactSfx Sound, int32 Variant);
	/** Every sound's every variant (to load them all up front). */
	CHAOSIMPACT_API void GetAllAssetPaths(TArray<FSoftObjectPath>& OutPaths);
	/** Sounds started since the game began (tests: -CISfxTrace also logs each). */
	CHAOSIMPACT_API int32 GetPlayCount(EChaosImpactSfx Sound);
}

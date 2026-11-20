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
	// (Added after: each has its own sound where another's used to be borrowed. New ones go at the end: the RPG sends these by number.)
	// スワップ / バリア / マイン
	SwapWarp, BarrierUp, BarrierBreak, BarrierFade, MineSet, MineArm, MineAlarm, MineBlast,
	// The hook and the anchor
	HookFire, AnchorFire, HookCatch, HookGrab, HookClank, AnchorStick, AnchorZip, AnchorRam,
	// Bodies: the tackle, the weight under a hit, a throw's swish, a revive, the ball form, a snowball run over someone, ice letting go
	TackleStart, TackleHit, CharThud, CharWhoosh, CharRevive, FormCurl, FormUncurl, SnowCrush, IceThaw,
	// The crash ball, the party rules' rubies and crown
	CrashHit, CrashBump, RubyPick, RubyBig, RubyLand, CrownTake, CrownDrop,
	// The nova's parts and end, a ball coming from off the screen
	NovaPart, NovaLand, NovaBreak, BallWarn,
	// Stage hazards: the warning, a gate forming, a bounce off one, a vent erupting, a pillar of light
	StageCharge, StageGate, StageBounce, StageErupt, StageSmite,
	// The dojo's rings, a treasure chest, coins
	DojoRing, ChestOpen, CoinPick,
	// The duel's own calls: a tournament match's opening, the ball slot, ROUND, FIGHT!, SUDDEN DEATH, a round decided, WIN
	DuelOpening, DuelReelSpin, DuelReelStop, DuelReelDone, DuelRound, DuelFight, DuelSudden, DuelRoundOver, DuelWin,
	// 合体ボール: two balls drawn together into one (with its flash), and parted again
	FusionJoin, FusionSplit,
	// The boss scenes (シマエナガ戦): the coming, the fall, the landing, the war cry, the name card, the camera's swing, the
	// beaten boss flying off; a line's letters appearing, and on to the next line
	SceneRumble, SceneFall, SceneLand, SceneRoar, SceneTitle, SceneWhoosh, SceneFlee, TalkBlip, TalkNext, SceneGlint,
	// 回復ボール (2026-11-19, solo only): its spot opening, a heal, the spot running out unused.
	HealSpot, HealGain, HealFade,
	// ラスボス ドッチ・ボール大統領 (2026-11-19): its moves.
	PresThrow, PresHatch, PresBeamCharge, PresBeamFire, PresBeamEnd, PresCarTransform, PresCarRev, PresCarRush, PresCarHorn,
	PresNovaCharge, PresNovaLaunch, PresNovaFall, PresNovaBlast, PresStun, PresPowerUp,
	// Its hall's scenes: the great doors, footsteps in the hall, the lights going out, the soldiers' stamp, a ball lighting
	// in a hand, the soldiers' rush, the hero's special ball (its wind-up and its blast), soldiers flying off into stars.
	HallDoor, HallStep, HallLightOff, HallStomp, HallBallLight, HallRush, HeroSpecialCharge, HeroSpecialBlast, HallStarFly,
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

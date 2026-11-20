#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBossScene.generated.h"

class ACameraActor;
class AChaosImpactCharacter;
class AChaosImpactFxKit;
class AChaosImpactSimaeBoss;
class AChaosImpactSimaePerch;
class AChaosImpactPresidentBoss;
class AChaosImpactWhiteHall;
class USpotLightComponent;
class AChaosImpactSoloRoom;
class SWidget;
class UPointLightComponent;
struct FGeometry;
class FSlateWindowElementList;

/**
 * ソロモードのシマエナガ戦の演出 (2026-11-19). The player comes into the snowfield's arena: the screen goes dark and the boss's
 * arrival plays as a short film (the little shima-enaga on the snow see it coming and fly off; it drops out of the sky; the
 * snow it throws up hangs as a fog; red eyes light up in it and it steps out; it roars at the sky, speed lines and its
 * name), then the two talk (the script before the fight) and the fight starts. Beaten, it talks again (the script after)
 * and flies off; the stage is cleared.
 *
 * The talk is drawn like a story game's: the two stand in the arena, the camera at their side with the far snow out of
 * focus, a slanted box along the bottom with the speaker's name, the words typed out (ruby over 避球者), ▼ to go on.
 * The pause button skips (the film, then the talk); tap or the decide button goes on. Offline (solo mode) only.
 */
UCLASS(NotPlaceable, Transient)
class AChaosImpactBossScene : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactBossScene();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The arena's boss is met: with bArrival the film and the talk first (else straight to the fight, watching for its defeat). */
	static AChaosImpactBossScene* Begin(AChaosImpactSoloRoom* Room, AChaosImpactCharacter* Hero, AChaosImpactSimaeBoss* Boss, bool bArrival);
	/**
	 * ラスボス (2026-11-19): the President's scenes in the White Ball House's great hall: the arrival (the doors, the lined
	 * soldiers, the lights going out, the six balls lit, its name), the words before the fight (the soldiers' rush and the
	 * hero's special ball), and after it the words, then the ending (a film's end roll) and its last little scene.
	 */
	static AChaosImpactBossScene* BeginPresident(AChaosImpactSoloRoom* Room, AChaosImpactCharacter* Hero, AChaosImpactPresidentBoss* Boss, bool bArrival);
	/** The ending is playing (the music keeps quiet for it). */
	static bool IsEnding(const UWorld* World);
	static AChaosImpactBossScene* GetActive(const UWorld* World);

	/** Where it is. */
	enum class EPart : uint8 { Arrival, TalkBefore, ToFight, Fight, Beaten, TalkAfter, Leave, Ending, Done };
	EPart GetPart() const { return Part; }
	float GetPartTime() const { return PartTime; }
	/** It has the screen (the pause button skips, the player is held). */
	bool IsTakingInput() const { return Part != EPart::Fight && Part != EPart::Done; }
	/** The pause button: the film skipped to the talk, the talk to its end. */
	void RequestSkip();
	/** The decide button or a tap: the line finished at once, or on to the next. */
	void RequestNext();

	/** Drawn over the game (its own viewport widget). */
	void PaintOverlay(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;

	/** Tests: the line shown now (-1: none) and how many lines the part has. */
	int32 GetLineIndex() const { return LineIndex; }
	int32 GetLineCount() const;

private:
	struct FLine
	{
		bool bBoss = false;
		/** The words; {base|reading} puts the reading over base as ruby. */
		FString Text;
		/** 0 plain, 1 a shout (a shake), 2 the war cry (roar, flames, flash), 3 a puzzled ? (a quick push in); 10+ the President's. */
		int32 Emote = 0;
		/** Who speaks when it is neither the hero nor the boss: 1 the soldiers, 2 the two together. */
		uint8 Who = 0;
	};
	const TArray<FLine>& Lines() const;
	void SetPart(EPart NewPart);
	void TickArrival(float Time);
	void TickTalk(float DeltaSeconds);
	void TickToFight(float Time);
	void TickBeaten(float Time);
	void TickLeave(float Time);
	void StartLine(int32 Index);
	void Hold(bool bHeld);
	void PlaceTalk();
	void SetCamera(const FVector& At, const FVector& LookAt, float Fov, float FocusBlur = 0.0f);
	void Shake(float Strength, float Seconds);
	void ReadInput();
	void PlayFog(const FVector& Center, const FVector& CameraAt);
	void PlayFaceWisps(const FVector& Face, const FVector& Forward, const FVector& Across);
	void PlayFogParting(const FVector& At, const FVector& Dir, const FVector& Side);
	void ShowFlakes();
	void UpdateEyes(float Glow);
	void Finish();

	// ---- The President's (ChaosImpactBossScenePresident.cpp).
	AActor* GetFoe() const;
	bool IsFoeBeaten() const;
	void TickArrivalPresident(float Time);
	void TickTalkPresident(float DeltaSeconds);
	void StartLinePresident(const FLine& Line);
	void TickToFightPresident(float Time);
	void TickEnding(float Time);
	void SkipArrivalPresident();
	void SpawnRunners();
	void TickRunners(float DeltaSeconds);
	void ClearRunners();
	void PaintEnding(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;
	FString GetSpeakerName(const FLine& Line) const;
	TWeakObjectPtr<AChaosImpactPresidentBoss> President;
	TWeakObjectPtr<AChaosImpactWhiteHall> Hall;
	bool bPresident = false;
	/** The lined soldiers who run at the hero at the President's word (the hall's own, taken off their places), and the gag. */
	TArray<int32> Runners;
	TArray<FVector> RunnerAt;
	TArray<FVector> RunnerFlight;
	TArray<int32> BlownAway;
	TWeakObjectPtr<AActor> Gag;
	UPROPERTY()
	TObjectPtr<USpotLightComponent> ShadowLight;
	UPROPERTY()
	TObjectPtr<UPointLightComponent> FaceLight;
	FVector SpecialAt = FVector::ZeroVector;
	float SpecialTime = -1.0f;
	float LeapTime = -1.0f;
	FVector LeapFrom = FVector::ZeroVector;
	FVector LeapTo = FVector::ZeroVector;
	float EndingSkipHold = 0.0f;
	float CreditsAge = -1.0f;
	float FinAge = -1.0f;
	FString GagLine;
	float GagAge = -1.0f;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AChaosImpactSoloRoom> Room;
	TWeakObjectPtr<AChaosImpactCharacter> Hero;
	TWeakObjectPtr<AChaosImpactSimaeBoss> Boss;
	TArray<TWeakObjectPtr<AChaosImpactSimaePerch>> Birds;
	/** The stage's drifting flakes, put away while the scene plays (dark specks against the sky up close). */
	TArray<TWeakObjectPtr<AActor>> HiddenFlakes;
	/** Its eyes' red lights, one each (kept a little toward the camera from the eye, never inside its head). */
	TWeakObjectPtr<AChaosImpactFxKit> EyeFx[2];
	UPROPERTY()
	TObjectPtr<UPointLightComponent> EyeLight;
	TSharedPtr<SWidget> Overlay;

	EPart Part = EPart::Arrival;
	float PartTime = 0.0f;
	int32 Cue = 0;
	bool bSlamStarted = false;
	FVector BossHome = FVector::ZeroVector;
	FRotator BossHomeRotation = FRotator::ZeroRotator;
	FVector HeroAt = FVector::ZeroVector;
	FVector TalkCamera = FVector::ZeroVector;
	FVector TalkLook = FVector::ZeroVector;
	FVector TalkSide = FVector::ZeroVector;
	FVector CamAt = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	float CamFov = 50.0f;
	float ShakeStrength = 0.0f;
	float ShakeLeft = 0.0f;
	float ShakeTotal = 1.0f;

	// The talk.
	bool bAfter = false;
	int32 LineIndex = -1;
	float LineTime = 0.0f;
	float Typed = 0.0f;
	int32 BlipAt = 0;
	float EmoteTime = -1.0f;

	// What is drawn (0..1 each).
	float Black = 0.0f;
	float Letterbox = 0.0f;
	float Flash = 0.0f;
	/** The snow cloud over the camera: the screen whited out. */
	float White = 0.0f;
	float TitleTime = -1.0f;
	float BoxShown = 0.0f;
	float HudShown = 1.0f;
	bool bTouchWasDown = false;
	mutable FBox2D SkipButton = FBox2D(ForceInit);
	mutable FBox2D NextArea = FBox2D(ForceInit);
};

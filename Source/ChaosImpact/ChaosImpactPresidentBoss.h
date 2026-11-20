#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPresidentBoss.generated.h"

class AChaosImpactFxKit;
class AChaosImpactSimaeWarning;
class AChaosImpactSoloRoom;
class AChaosImpactWhiteHall;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USkeletalMesh;
class UStaticMeshComponent;
class FSlateWindowElementList;
struct FGeometry;

UENUM()
enum class EChaosImpactPresidentMove : uint8
{
	/** Floating about, sitting cross-legged in the air, between attacks. */
	None,
	/** 6連投: a different kind of ball from each of its six hands in turn. */
	Throws,
	/** お腹ビーム: the hatch in its belly opens, a red line, then a great beam sweeping slowly after its target. */
	Beam,
	/** 車突進: it turns into its car, drives off, a warning across the hall, and comes through at full speed. */
	Car,
	/** 6連ノヴァ (from half health): six small novas, one after another, each where its target is; then it is spent. */
	NovaStorm,
	/** Spent after its novas: down on the floor a while, wide open. */
	Stunned,
	/** At half health: the change (it cannot be hit meanwhile); the hall goes red. */
	PowerUp
};

/**
 * ラスボス ドッチ・ボール大統領 (2026-11-19): the six-armed President of the United States of Dett Ball, in the great hall of
 * the White Ball House (solo stage 30). It sits cross-legged in the air the whole fight and glides about. Its moves follow
 * the user's plan (ラスボス.txt), made fair: every hit is 1, a dash goes through everything, warnings never overlap, the
 * novas are small and can be run from. Half its health gone: it powers up (the hall turns red) and opens with its six
 * novas; after them it is spent a while. With a scene to follow (AChaosImpactBossScene) its last hit leaves it beaten.
 * Solo mode only (it plays itself on this machine; nothing is sent to guests).
 */
UCLASS()
class CHAOSIMPACT_API AChaosImpactPresidentBoss : public AChaosImpactCharacter
{
	GENERATED_BODY()

public:
	AChaosImpactPresidentBoss();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
		AActor* DamageCauser) override;
	virtual bool IsMoveProof() const override;

	/** Asleep: still and unhurt (on its throne before the fight). Waking starts the fight. */
	void SetDormant(bool bNewDormant);
	bool IsDormant() const { return bDormant; }
	bool IsFullPower() const { return bFullPower; }
	EChaosImpactPresidentMove GetMove() const { return Move; }
	/** Tests: starts a move now. */
	void ForceMove(EChaosImpactPresidentMove NewMove);
	/** Tests: its attacks hurt nobody. */
	bool bHarmless = false;
	/** Tests: it never starts a move by itself. */
	bool bHoldMoves = false;

	// ---- Its scenes (AChaosImpactBossScene).
	void SetSceneHold(bool bHold);
	bool IsSceneHeld() const { return bSceneHold; }
	void SetSceneDefeat(bool bOn) { bSceneDefeat = bOn; }
	bool IsBeaten() const { return bBeaten; }
	void FinishBeaten();
	/** Its clips for a scene: 0 sitting, 1-6 a throw by each arm, 7 beam start, 8 beam fire, 9 beam end. */
	void PlaySceneClip(int32 Clip, bool bLoop, float Rate);
	/** How high above the floor it floats (0: sitting on whatever is under it), eased there. */
	void SetHover(float Height, float Seconds = 0.4f);
	/** Its six hands (0-5: right upper, middle, lower, left upper, middle, lower): where a ball sits in each. */
	FVector GetHand(int32 Arm) const;
	/** Its face (for a scene's close shots). */
	FVector GetFace() const;
	/** A ball shown glowing in a hand (a scene's six lighting up, its throws' wind-up). Normal: none. */
	void ShowHandBall(int32 Arm, EChaosImpactBallType Type);
	void HideHandBalls();
	/** How tall it is as it sits (for cameras). */
	float GetSeatedHeight() const { return SeatedHeight; }

	/** The bar across the top of the screen in its fight (its name and its health only). */
	static void PaintHud(const UWorld* World, const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, float Scale);

	/** How big it is (the model's own size times this). */
	UPROPERTY(EditAnywhere, Category="Chaos Impact|President")
	float SeatedHeight = 470.0f;

	// The moves' numbers.
	/** How high it floats, sitting cross-legged, between its moves (low: a ball thrown at it is seen to hit its body). */
	static constexpr float IdleHover = 45.0f;
	static constexpr float ThrowSpeed = 1750.0f;
	static constexpr float ThrowInterval = 0.55f;
	static constexpr float BeamWarnSeconds = 1.1f;
	static constexpr float BeamFireSeconds = 1.5f;
	static constexpr float BeamRange = 2900.0f;
	static constexpr float BeamHalfWidth = 105.0f;
	static constexpr float BeamTurnDegrees = 20.0f;
	static constexpr float CarWarnSeconds = 0.8f;
	static constexpr float CarSpeed = 5200.0f;
	static constexpr float CarHalfWidth = 135.0f;
	static constexpr int32 CarPassCount = 6;
	static constexpr int32 FullPowerCarPassCount = 8;
	static constexpr float CarPassInterval = 0.85f;
	static constexpr int32 NovaCount = 6;
	static constexpr float NovaInterval = 0.8f;
	static constexpr float NovaWarnSeconds = 1.15f;
	static constexpr float NovaRadius = 480.0f;
	static constexpr float StunSeconds = 3.0f;
	static constexpr float PowerUpSeconds = 2.6f;

private:
	void ThinkIdle(float DeltaSeconds);
	void ChooseMove();
	void BeginMove(EChaosImpactPresidentMove NewMove);
	void EndMove(float Rest);
	void AbortMove();
	void TickThrows(float DeltaSeconds);
	void TickBeam(float DeltaSeconds);
	void TickCar(float DeltaSeconds);
	void TickNovas(float DeltaSeconds);
	void TickStunned(float DeltaSeconds);
	void TickPowerUp(float DeltaSeconds);
	void StartPowerUp();
	void UpdateHover(float DeltaSeconds);
	void UpdateBeamLook(float Shown);
	void ThrowFrom(int32 Arm);
	void DropNova(const FVector& Spot);
	AChaosImpactCharacter* FindTarget() const;
	bool IsFoe(const AChaosImpactCharacter* Character) const;
	bool StrikeFoe(AChaosImpactCharacter* Victim, const FVector& Knockback);
	FVector KeepInArena(const AChaosImpactCharacter* Victim, const FVector& Knockback) const;
	const AChaosImpactSoloRoom* FindHomeRoom() const;
	FVector ArenaPoint(const FVector& Wanted, float Margin) const;
	void GetArena(FVector& OutCenter, FVector& OutExtent) const;
	void TurnToward(const FVector& Direction, float DeltaSeconds, float DegreesPerSecond);
	void PlayClip(UAnimSequence* Clip, bool bLoop, float Rate);
	void ShakeNearby(const FVector& Where, float Radius, float Strength, float Seconds) const;
	float Tempo() const { return bFullPower ? 1.2f : 1.0f; }
	void SetCarForm(bool bCar);
	/** Where each of its six palms is, worked out from the model (the middle of the hand's own skin), in its bone's space. */
	void FindHands();
	FName HandBones[6];
	FVector HandOffsets[6];
	bool bHandsFound = false;
	AChaosImpactWhiteHall* FindHall() const;

	UPROPERTY() TObjectPtr<USkeletalMesh> BodyModel;
	UPROPERTY() TObjectPtr<USkeletalMesh> CarFormModel;
	UPROPERTY() TObjectPtr<UAnimSequence> SitClip;
	UPROPERTY() TObjectPtr<UAnimSequence> ThrowClips[6];
	UPROPERTY() TObjectPtr<UAnimSequence> BeamStartClip;
	UPROPERTY() TObjectPtr<UAnimSequence> BeamFireClip;
	UPROPERTY() TObjectPtr<UAnimSequence> BeamEndClip;
	UPROPERTY() TObjectPtr<UAnimSequence> CarStartClip;
	UPROPERTY() TObjectPtr<UAnimSequence> CarEndClip;
	UPROPERTY() TObjectPtr<UAnimSequence> PlayingClip;
	bool bPlayingLoop = false;

	/** The cars it drives as (on their own: the boss's body is put away meanwhile); two, so one can come while the last goes. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Car;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Car2;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> CarLights;
	/** The beam: a white-hot core and a red glow round it, from its belly. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BeamCore;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BeamGlow;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> BeamLight;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BeamCoreLook;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BeamGlowLook;
	/** The balls glowing in its hands. */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HandBalls;
	TArray<EChaosImpactBallType> HandBallTypes;
	TWeakObjectPtr<AChaosImpactFxKit> Fx;
	AChaosImpactFxKit* GetFx();

	EChaosImpactPresidentMove Move = EChaosImpactPresidentMove::None;
	float MoveTime = 0.0f;
	int32 MoveStage = 0;
	int32 Shots = 0;
	float RestLeft = 2.0f;
	int32 MovesSinceNovas = 0;
	EChaosImpactPresidentMove LastMove = EChaosImpactPresidentMove::None;
	TWeakObjectPtr<AChaosImpactCharacter> MoveTarget;
	TArray<EChaosImpactBallType> ThrowKinds;
	TArray<int32> ArmOrder;
	FVector MoveDirection = FVector::ForwardVector;
	FVector CarStart = FVector::ZeroVector;
	FVector CarEnd = FVector::ZeroVector;
	FVector CarComeBack = FVector::ZeroVector;
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> HitThisMove;
	/** 車突進: one car's run down the hall (seen from straight above): its lane across, when it is warned and when it comes. */
	struct FCarPass
	{
		/** Its way across the floor (flat), the line it runs along (through Through), and where it drives in from and out to. */
		FVector Dir = FVector(-1.0f, 0.0f, 0.0f);
		FVector Through = FVector::ZeroVector;
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		float WarnAt = 0.0f;
		float RushAt = 0.0f;
		int32 CarIndex = 0;
		bool bWarned = false;
		bool bRushing = false;
		bool bDone = false;
		TWeakObjectPtr<AChaosImpactSimaeWarning> Warning;
		TSet<TWeakObjectPtr<AChaosImpactCharacter>> Hit;
	};
	TArray<FCarPass> CarPasses;
	/** This car move's pattern (down the hall, across it, slantwise, any way, or two at once crossing). */
	int32 CarPattern = 0;
	int32 LastCarPattern = INDEX_NONE;
	void PlaceCar(UStaticMeshComponent* Which, const FVector& At, const FVector& Facing) const;
	void CarHits(UStaticMeshComponent* Which, FCarPass& Pass);
	TWeakObjectPtr<AChaosImpactSimaeWarning> Warning;
	struct FNovaShot
	{
		FVector Spot;
		/** It lands at DueAt; its ball of fire starts at ExplodeAt (the VS nova's spin before the blast). */
		float DueAt;
		float ExplodeAt;
		TWeakObjectPtr<AChaosImpactSimaeWarning> Warning;
		/** Coming down: the VS nova's own look, on a component of its own. */
		TWeakObjectPtr<USceneComponent> Fall;
		ChaosImpactBallTypes::FNovaLook FallLook;
		bool bLanded = false;
		bool bDone = false;
		TSet<TWeakObjectPtr<AChaosImpactCharacter>> Hit;
	};
	void ClearNovaFall(FNovaShot& Shot);
	TArray<FNovaShot> NovaShots;
	float NextStrafeChange = 0.0f;
	float StrafeSign = 1.0f;

	bool bDormant = false;
	bool bFullPower = false;
	bool bSceneHold = false;
	bool bSceneDefeat = false;
	bool bBeaten = false;
	float WakeTime = -1.0f;
	float ShownHealth = -1.0f;
	float HitFlash = 0.0f;
	float Hover = 120.0f;
	float HoverFrom = 120.0f;
	float HoverTo = 120.0f;
	float HoverTime = 1.0f;
	float HoverSeconds = 0.4f;
	float ModelScale = 1.0f;
	float ModelBottom = 0.0f;
	FVector MeshRest = FVector::ZeroVector;
	mutable bool bLookedForRoom = false;
	mutable TWeakObjectPtr<const AChaosImpactSoloRoom> HomeRoom;
};

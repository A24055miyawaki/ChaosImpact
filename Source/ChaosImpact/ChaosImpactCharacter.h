// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/NetSerialization.h"
#include "Logging/LogMacros.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactEasyControls.h"
#include "ChaosImpactTouchControls.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactFusionEffects.h"
#include "InputCoreTypes.h"
#include "ChaosImpactCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UChaosImpactShoulderCameraComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class UInputAction;
class UChaosImpactChargeWidget;
class UAnimSequenceBase;
class UAnimInstance;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UProceduralMeshComponent;
class UAudioComponent;
class AChaosImpactBall;
class UChaosImpactBallFormComponent;
class UChaosImpactTackleComponent;
enum class EChaosImpactNovaPart : uint8;
class AChaosImpactTornado;
class AChaosImpactHook;
class AChaosImpactGadget;
class AChaosImpactHazardZone;
class UChaosImpactPuppetComponent;
enum class EChaosImpactBallFlightMode : uint8;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/** Any character hit (Victim, the damage done, what did it, who threw it): native, for the dojo's counting. */
DECLARE_MULTICAST_DELEGATE_FourParams(FChaosImpactAnyCharacterDamaged, class AChaosImpactCharacter*, float, AActor*, APawn*);

/** A character has just been knocked out (see AChaosImpactCharacter::OnEliminated). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChaosImpactCharacterEliminated, AChaosImpactCharacter*, Character);

/**
 *  Player character for the top-down dodgeball prototype.
 *  Movement is camera-relative while the character faces the current aim point.
 */
UCLASS(abstract)
class AChaosImpactCharacter : public ACharacter
{
	GENERATED_BODY()
	friend class UChaosImpactBallFormComponent;
	friend class UChaosImpactTackleComponent;
	friend class AChaosImpactBuddyBall;
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Ball Form")
	TObjectPtr<UChaosImpactBallFormComponent> BallForm;
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Tackle")
	TObjectPtr<UChaosImpactTackleComponent> Tackle;

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UChaosImpactShoulderCameraComponent* ShoulderCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	AChaosImpactCharacter();
	/** ペイント now on the model (0: none), and whether it is on the model shown now (for checks). */
	int32 GetShownPaintKey() const { return ShownPaintOn.IsValid() ? ShownPaintKey : 0; }
	/** Speeds up take-off and landing without reducing the authored jump height. 1 restores the old arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Jump", meta=(ClampMin="1.0", ClampMax="2.5"))
	float JumpTempo = 1.7f;
	/** Converts a pre-tempo vertical launch impulse; horizontal impulses stay unchanged. */
	float GetVerticalImpulseTempo() const { return AppliedJumpTempo; }
	bool IsShoulderViewActive() const;
	UChaosImpactShoulderCameraComponent* GetShoulderCamera() const { return ShoulderCamera; }
	/** Duel side 1 looks from the opposite end; never changes the saved BP camera or world-space throws. */
	FRotator GetTopDownCameraRotation() const;
	void SetGameplayUIVisible(bool bVisible);
	void CancelChargingThrow();
	/** Server: knocked loose: every ball carried falls and rolls away (a charge is called off). */
	virtual void LoseCarriedBalls();
	/** This character's tackle has just hit someone (the host). */
	virtual void OnTackledSomeone(AChaosImpactCharacter* Victim) {}
	void BeginThrowInput();
	/** Hardware input only: synchronizes the polling fallback without changing the scripted/CPU API. */
	void BeginBoundThrowInput();
	void EndThrowInput();
	virtual bool HandleEasyPointerKey(const FKey& Key, EInputEvent Event);
	virtual bool IsPointerOverGameplayUI() const;
	void ResetEasyControls();
	/** スマホ版: this player plays by touch (a phone, or -CITouch on a PC). */
	bool IsTouchControlled() const;
	/**
	 * スマホ版を縦に持っているとき: 0 (横・正方形) .. 1 (9:16 より細い). The camera then keeps its width (not its height),
	 * a little narrower and further back, so the field is not seen through a slit.
	 */
	float GetUprightViewBlend() const;
	/** The knockout shards follow the character (while they fly) or stay still in the world (hidden: no per-frame updates). */
	void SetEliminationPiecesFollow(bool bFollow);
	/** (Measuring: this player's in-play HUD.) */
	class UUserWidget* GetChargeWidgetForTests() const;
	static constexpr float UprightFieldOfView = 74.0f;
	static constexpr float UprightArmScale = 1.22f;
	/** スマホ版: the left thumb's stick as drawn (screen pixels), while it is down. */
	bool GetTouchStick(FVector2D& OutAnchor, FVector2D& OutThumb) const;
	/** スマホ版: the fingers' last frame (the HUD draws the buttons held, the stick, the throw's drag), and the style. */
	const FChaosImpactTouchFrame& GetLastTouchFrame() const { return LastTouchFrame; }
	/** The placing of the controls and the HUD in use now (設定 → ボタン・画面の配置, for this kind of play). */
	const FChaosImpactTouchCustom& GetTouchCustom() const { return TouchGestures.Custom; }
	bool IsTouchSwapShown() const { return TouchGestures.bSwapShown; }
	bool IsTouchFuseShown() const { return TouchGestures.bFuseShown; }
	bool IsTouchAimStickShown() const { return TouchGestures.bAimStickShown; }
	bool IsTouchButtonStyle() const { return TouchGestures.bButtons; }
	/** 片手モード (the third touch style). */
	bool IsTouchOneHand() const { return TouchGestures.bOneHand && !TouchGestures.bButtons; }
	bool IsTouchLeftHanded() const { return TouchGestures.bLeftHanded; }
	/** Tests: fingers as if on the screen (used instead of the real ones until cleared). */
	void SetTouchForTests(const TArray<FVector2D>& Down) { bTouchForTests = true; TouchForTests = Down; }
	void ClearTouchForTests() { bTouchForTests = false; TouchForTests.Reset(); }
	bool HasClickMoveGoal() const { return bClickMoveActive; }
	FVector GetClickMoveGoal() const { return ClickMoveGoal; }
	bool WasClickMoveRejected() const { return bClickMoveRejected; }
	void RecoverStaminaFromBallHit();
	/** Called on the eliminating character so its own HUD can play the KO banner. */
	void NotifyOpponentEliminated(const FString& VictimName);
	/** A hit that did not knock out: the HIT card at the top right (bDealt: this player hit Name; else Name hit them). */
	void NotifyHitFeed(bool bDealt, const FString& Name);
	/** Rumbles this character's controller when it is played on this machine (see AChaosImpactPlayerController::PlayRumble). */
	void PlayControllerRumble(float Small, float Big, float Seconds, float DelaySeconds = 0.0f) const;
	bool TryPickupBall(AChaosImpactBall* Ball);
	/** Server: a ball someone else had just taken goes to this player, who touched it earlier (AChaosImpactBall::TakeOverPickup). */
	bool GiveContestedPickup(EChaosImpactBallType Type);
	/** Server: the ball just picked up (the last one in hand) goes to someone who touched it earlier. */
	void TakeBackLastPickup();
	/** Carried ball type per slot; slot 0 is the right hand and is thrown next. */
	EChaosImpactBallType GetCarriedBallType(int32 Slot) const
	{
		return ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, Slot);
	}
	uint16 GetCarriedBallTypesPacked() const { return CarriedBallTypes; }
	/** Swaps the two carried balls so the left-hand ball is thrown next. Needs two balls in hand. */
	virtual void RequestBallSwap();
	/** 合体ボール (prototype): the two balls in hand can be joined now (training, an online room's lobby; never a match). */
	bool CanFuseBalls() const;
	/** The two balls in hand are joined: the next throw throws both as one. */
	bool AreBallsFused() const { return bBallsFused; }
	/** 合体 with a ノヴァ riding along on the last throw: how big it was charged (its blast's scale; 0: none). */
	float GetFusedNovaScale() const { return FusedNovaScale; }
	/** Any machine: a fused snowball with a black hole in it, rolled along (重力雪玉): where it draws people in from. */
	bool GetFusedRollPull(FVector& OutCenter, float& OutRadius) const;
	/** The 合体 button: joins the two balls in hand (the one that flies goes in front), or parts them again. */
	void RequestBallFusion();
	/** Tests: these two in hand, nothing else (the server's / standalone). */
	void GiveBallsForTests(EChaosImpactBallType Front, EChaosImpactBallType Back);
	/** Tests: throws what is in hand now along Aim, as a released charge would (online, a guest's goes to the host as theirs do). */
	bool ThrowForTests(const FVector& Aim, float ChargeAlpha);
	/** Tests, online: the host puts these two in this guest's hands (nothing in a shipping build). */
	UFUNCTION(Server, Reliable)
	void ServerGiveBallsForTests(EChaosImpactBallType Front, EChaosImpactBallType Back);
	/** 訓練場's filming: this player's aim and actions come from the script, not the keys and the mouse. */
	void SetDojoScripted(bool bOn) { bDevAutoInput = bOn; }
	/** 訓練場: how far the camera looks ahead of this player, toward the lesson's goals (zero: on the player). */
	void SetDojoCameraLead(const FVector& Lead) { DojoCameraLead = Lead; }
	/** 訓練場: held to the spot (no walking, no dash) for the lessons that are about aiming (targets, the drive course). */
	void SetDojoRooted(bool bOn) { bDojoRooted = bOn; }
	bool IsDojoRooted() const { return bDojoRooted; }
	/** Drops the ball in hand (the one thrown next) on the floor in front, where anyone can pick it up (F, D-pad left). */
	virtual void RequestDropBall();
	UChaosImpactBallFormComponent* GetBallForm() const { return BallForm; }
	bool IsInBallForm() const;
	UChaosImpactTackleComponent* GetTackle() const { return Tackle; }
	/** A model of its own (an RPG boss's) whatever its player state says; INDEX_NONE for the usual pick. */
	int32 LookOverride = INDEX_NONE;
	FVector GetBuddyHoldLocation(int32 Slot) const;
	/** Snow: how far (0-1) the snowball in Slot has grown as its carrier walked; 0 for any other ball. */
	float GetSnowGrowth(int32 Slot) const;
	/** Cancels a throw being charged (right mouse button, ZL / L2); the ball stays in hand. */
	void RequestCancelThrow();
	/** Server: smoke got in this player's eyes; they can barely see for Seconds (the longer smoke wins). */
	void ApplyBlind(float Seconds);
	bool IsBlinded() const;
	/** 0-1: how thick the smoke over this player's view is now (1, clearing over its last second). */
	float GetBlindAmount() const;
	/** How long a full charge of this ball takes (a nova's is long). */
	float GetChargeSecondsFor(EChaosImpactBallType Type) const;
	/** A throw being charged as every machine sees it (the owner and server exactly, others from replication). */
	bool GetPresentedCharge(float& OutSeconds) const;
	/** Charging a nova: rooted to the spot, arms up, the nova swelling over the head. On every machine. */
	bool IsChargingNova() const;
	/** Steering a thrown drive ball: rooted to the spot, the camera on the ball, movement turning it. */
	bool IsDriving() const;
	AChaosImpactBall* GetDrivenBall() const { return DrivenBall.Get(); }
	/** The driver's own screen: tells the host where its drive ball is (a few dozen times a second). */
	void SendOwnDriveState(AChaosImpactBall* Ball);
	UFUNCTION(Server, Unreliable)
	void ServerDriveState(AChaosImpactBall* Ball, FVector_NetQuantize Location, FVector_NetQuantizeNormal Heading);
	double NextDriveStateAt = 0.0;
	/** CPUs: which way to steer the drive ball (flat). */
	void SetAIDriveSteer(const FVector& Direction);
	/** Ends the drive ball where it is, as a wall would (the throw button again while steering). */
	void StopDrivenBall();
	/** This screen's throw preview of a drive ball was replaced by the server's ball: steer that one now. */
	void OnDriveBallAdopted(AChaosImpactBall* Predicted, AChaosImpactBall* Adopted);
	/**
	 * Where the ball in hand would come down if thrown now with this charge (its first contact with the stage),
	 * and how wide an area it covers there (a nova: its blast; a snowball: its size). False when it would fly off.
	 */
	bool PredictThrowLanding(float ChargeAlpha, FVector& OutGround, float& OutRadius) const;
	/** The ball in hand's flight if thrown now: where it first touches the stage (its centre), and the path (OutArc). */
	bool PredictThrowContact(float ChargeAlpha, FVector& OutContact, TArray<FVector>* OutArc = nullptr) const;
	/** A barrier ball in hand: the floor its wall would stand on and the way it would face (the throw), and the path. */
	bool PredictBarrierPlacement(float ChargeAlpha, FVector& OutFloor, FVector& OutHeading, TArray<FVector>* OutArc = nullptr) const;
	/** Tests: whether the barrier preview is showing. */
	bool IsBarrierPreviewShown() const;
	/** Selects a nova's ground target. The point is clamped and projected onto the playable stage. */
	bool SetNovaTargetPoint(const FVector& WorldPoint);
	bool HasNovaTargetPoint() const { return bNovaTargetValid; }
	FVector GetNovaTargetPoint() const { return FVector(NovaTargetLocation); }
	/** How a throw leaves: its speeds and flight, and whether it goes from over the head instead of the hand. */
	void GetThrowFlight(float ChargeAlpha, EChaosImpactBallType Type, float Scale, float& OutHorizontalSpeed,
		float& OutUpSpeed, EChaosImpactBallFlightMode& OutMode, bool& bOutOverhead) const;
	/** Where a ball held up over the head sits, from the capsule's centre. */
	FVector GetOverheadHoldOffset(EChaosImpactBallType Type, float Scale) const;
	/**
	 * How fast a big snowball leaves over the head: FullSpeed, or slower so it comes down on the nearest opponent standing
	 * in its way (ahead, within its full reach and within its width of the aim) instead of sailing over them.
	 */
	/**
	 * A big snowball hurled from over the head: always at FullSpeed; aimed at someone near in front it leaves the hand
	 * flatter (OutUpSpeed, down when need be) so it comes down on them, as a throw at someone far off does.
	 */
	float GetOverheadSnowSpeed(float Scale, float FullSpeed, float* OutUpSpeed = nullptr) const;
	/** Seconds until this character could start a dash (0: now): stamina, the cooldown and a dash in progress. */
	float GetDashReadyInSeconds() const;
	/** Seconds left of the dash in progress (0 when not dashing). */
	float GetDashRemainingSeconds() const;
	/** Server: what last took health off this character (a ball, a burst's zone...), and when. */
	AActor* GetLastDamageCauser() const { return LastDamageCauser.Get(); }
	/**
	 * A throw already released and about to leave the hand (the moment between the release and the ball flying): the
	 * ball, which way and how fast it will go, and in how many seconds. False when there is none.
	 */
	bool GetPendingThrow(AChaosImpactBall*& OutBall, FVector& OutDirection, float& OutSpeed, float& OutUpSpeed, bool& bOutArc,
		float& OutSecondsLeft) const;
	double GetLastDamagedAt() const { return LastDamagedAt; }
	/** Blown off the feet by a blast (applied on whichever machine moves this character). */
	void ApplyBlastKnockback(const FVector& Velocity);
	/**
	 * Not moved about by anything just now (a boss in the middle of an attack): winds, hooks, pulls, swaps, blasts, ice and
	 * capture all pass it by; hits still hurt. Whatever had hold of it lets go (ShakeOffMoveEffects).
	 */
	virtual bool IsMoveProof() const { return false; }
	void ShakeOffMoveEffects();
	/** A blast nearby shakes this player's camera (Strength 0-1, fading over Seconds). */
	void AddCameraShake(float Strength, float Seconds);
	/** Name drawn above this character: CPUs are "CPU1", "CPU2"..., players use their room or local name. */
	FString GetOverheadDisplayName() const;
	/** The character and colour this player is shown as (the results podium shows the same). */
	int32 GetShownCharacter() const;
	int32 GetShownColour() const;
	/** The カスタムカラー parts the model wears now (empty: its usual colours). */
	const TArray<uint8>& GetShownCustomColours() const { return ShownCustomColours; }
	/** 1-based number shown above a CPU character; 0 for human players. */
	int32 GetCPUNumber() const { return CPUNumber; }

	/**
	 * パーティルール: this machine's player touched a ruby (its id) or the crown (ChaosImpactParty::CrownId) on their own
	 * screen; the host decides who touched it first and answers.
	 */
	void ClaimPartyItem(int32 Id);
	UFUNCTION(Server, Reliable)
	void ServerClaimPartyItem(int32 Id);
	UFUNCTION(Client, Reliable)
	void ClientPartyClaimResolved(int32 Id, bool bAccepted);
	void SetCPUNumber(const int32 Number) { CPUNumber = static_cast<uint8>(FMath::Clamp(Number, 0, 255)); }

	// ---- Placed in a level (solo mode enemies). A character placed in a level plays as a CPU by itself
	// (AIControllerClass is the VS mode's CPU); these set how. VS and training CPUs, spawned by the game mode, ignore them.

	/** How strong it plays as a CPU. Changing it later (Set CPU Level) takes effect at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetCPULevel, Category="Chaos Impact|Solo Enemy",
		meta=(ExposeOnSpawn="true", DisplayName="CPU Level (強さ)"))
	EChaosImpactCPULevel CPULevel = EChaosImpactCPULevel::Strong;

	/**
	 * Outside VS matches, characters with the same number are on one side: they never target or hurt each other.
	 * -1 (the default): placed CPUs all join side 1 (so enemies never fight each other) and players are on no side.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Enemy",
		meta=(ExposeOnSpawn="true", ClampMin="-1", DisplayName="Solo Team (チーム番号)"))
	int32 SoloTeam = -1;

	/** Off: when knocked out this CPU is gone for good (once its knockout has played) instead of coming back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Enemy",
		meta=(ExposeOnSpawn="true", DisplayName="Respawn After Knockout (倒されたら復活)"))
	bool bRespawnAfterElimination = true;

	/** The side placed CPUs join when SoloTeam is left at -1. */
	static constexpr int32 SoloEnemyTeam = 1;

	UFUNCTION(BlueprintSetter, Category="Chaos Impact|Solo Enemy")
	void SetCPULevel(EChaosImpactCPULevel Level);

	// ---- Solo mode (rooms)

	/** The moment this character is knocked out (a solo room starts over on its player's). Blueprints can bind it too. */
	UPROPERTY(BlueprintAssignable, Category="Chaos Impact|Solo")
	FChaosImpactCharacterEliminated OnEliminated;
	/** Every hit on any character, as it lands (server). */
	static FChaosImpactAnyCharacterDamaged OnAnyDamaged;

	/** Where this character comes back after its next knockout (a solo room sets its entrance). Nothing moves now. */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Solo")
	void SetRespawnPoint(FVector Location, FRotator Rotation);
	/** Server: an ice ball landed next to this player; they cannot move, dash or throw for Seconds. */
	void ApplyIceFreeze(float Seconds);
	bool IsIceFrozen() const;
	void SetTrainingStartTransform(const FVector& Location, const FRotator& Rotation);
	void SetAIAimDirection(const FVector& Direction);
	void RequestAIDash(const FVector& Direction);
	/** Freezes only character actions; the world and physics keep ticking. */
	void SetTrainingMenuFrozen(bool bFrozen);
	/** A scene (登場ムービー, 会話) over the game: this player's HUD out of sight while it plays (0..1 shown). */
	void SetSceneHudShown(float Shown);
	/** Server-only royale transitions; do not teleport, pause, cancel actions or clear in-flight balls. */
	void PrepareBattleSuddenDeath();
	void EliminateFromBattleRoyale();
	bool IsTrainingMenuFrozen() const { return bTrainingMenuFrozen; }
	/** On solo mode's world map: the player only walks (the map takes clicks and the confirm button), no battle HUD. */
	void SetMapWalker(bool bWalker);
	bool IsMapWalker() const { return bMapWalker; }
	/** The exploration map (OpenWorld/, removable): no knockout for falling far below the start, walking SpeedScale times as fast. */
	void SetExplorationMode(bool bOn, float SpeedScale = 1.0f) { bExploration = bOn; ExplorationSpeed = bOn ? SpeedScale : 1.0f; }
	/** The exploration map's small fry: knocked out by one hit. */
	void SetExplorationMinion() { MaxHealth = 1.0f; Health = 1.0f; }
	/** Moves P1 closer and to the side so the live settings panel does not cover them. */
	void SetTrainingMenuCameraActive(bool bActive);
	bool IsChargingThrow() const { return bIsChargingThrow; }
	bool IsThrowReleasePending() const { return bThrowReleasePending; }
	bool IsThrowAnimationPlaying() const { return bThrowAnimationActive; }
	bool IsPersonalAimGuideVisible() const;
	float GetAimGuideLength() const { return AimGuideLength; }
	/** The aim arrow's height over the character's middle (it lies flat there; mouse aim reads the cursor in that plane). */
	static constexpr float AimGuideHeight = 54.0f;
	FVector GetAimGuideStartWorldLocation() const;
	bool IsEliminated() const { return bEliminated; }
	/** The last-hit smoke is rising from this character. */
	bool IsShowingLastHitSmoke() const;
	/** Server copy of a player on another machine. Such players move and judge ball hits on their own client. */
	bool IsRemotePlayerOnServer() const;
	/** Client: this player's own screen saw a ball touch it; the server sanity-checks and applies it. */
	void ReportBallHitFromClient(AChaosImpactBall* Ball, const FVector& HitLocation);
	/** Client: this player's own screen saw a wind ball's tornado catch it; the server checks and applies the hit. */
	UFUNCTION(Server, Reliable)
	void ServerReportTornadoHit(AChaosImpactTornado* Tornado);
	/** Client: this player's own screen saw Attacker's tackle (started at Started) reach it; the server checks it. */
	UFUNCTION(Server, Reliable)
	void ServerReportTackleHit(AChaosImpactCharacter* Attacker, double Started);
	/** Client: this player's own screen saw Attacker's rolled snowball run it over; the server checks and applies it. */
	UFUNCTION(Server, Reliable)
	void ServerReportSnowRunOver(AChaosImpactCharacter* Attacker);
	/** Client: this player's own screen saw a burst catch it (not dashing there); the server checks and applies it. */
	UFUNCTION(Server, Reliable)
	void ServerReportBlastHit(AChaosImpactHazardZone* Zone);
	/**
	 * Blown away by a tornado: moved along Velocity (horizontal), easing out over Seconds. Takes effect where this
	 * character is moved (its owner, the host, a CPU), like a black hole's pull. bLift: blown off the ground a little too
	 * (off: pushed along the ground, feet down).
	 */
	void StartWindKnockback(const FVector& Velocity, float Seconds, bool bLift = true);
	/**
	 * Caught by a tornado: drawn in and whirled round it, lifted off the ground, for WindCarrySeconds, then thrown
	 * out. Called where the catch is judged (and on the server for everyone); the position moves where this character
	 * is moved, the lift and spin show on every machine.
	 */
	void BeginWindCarry(AChaosImpactTornado* Tornado);
	bool IsCarriedByWind() const { return WindCarrier != nullptr; }
	/**
	 * Caught by a hook: reeled in to just in front of its thrower, quickly, for at most AChaosImpactHook::PullMaxSeconds.
	 * Takes effect where this character is moved (its owner, the host, a CPU); a dash breaks free.
	 */
	void BeginHookPull(AChaosImpactHook* Hook);
	bool IsPulledByHook() const { return HookPuller != nullptr; }
	/** Every machine: this character's own hook is out (its arms follow the chain). */
	void ShowOwnHook(AChaosImpactHook* Hook);
	/** Knocked out of an anchor's flight (a stage's obstacle hit them): they fall from where they are. */
	void BreakAnchorZip() { if (bAnchorZipping) { EndAnchorZip(false); } }
	/** Server: health lost to a stage itself (a fall), with the usual hit presentation, never a knockout of itself. */
	void LoseHealthToStage(const float Amount) { Health = FMath::Clamp(Health - Amount, 0.0f, MaxHealth); OnPlayerHit(Health, Amount); ForceNetUpdate(); }
	/** Server: back to full health (a stage starting the player over). */
	void RefillHealth() { Health = MaxHealth; ForceNetUpdate(); }
	/** Server: Amount back (never over the maximum); what was really won back. */
	float RestoreHealth(const float Amount)
	{
		const float Before = Health;
		Health = FMath::Clamp(Health + FMath::Max(Amount, 0.0f), 0.0f, MaxHealth);
		ForceNetUpdate();
		return Health - Before;
	}
	/** A stage that wants a wider view (the crane yard's long hops): the top-down camera's distance times this. */
	void SetStageCameraScale(const float Scale) { StageCameraScale = FMath::Max(Scale, 0.1f); }
	/**
	 * A stage's own overhead view (the final boss's car rush, 2026-11-19): straight down over Center with HalfExtent all
	 * round in view, as a nova's aim shows the stage; off: the usual camera.
	 */
	void SetStageOverheadView(const bool bOn, const FVector& Center = FVector::ZeroVector, const float HalfExtent = 2400.0f)
	{
		bStageOverhead = bOn;
		StageOverheadCenter = Center;
		StageOverheadHalf = HalfExtent;
	}
	bool IsStageOverheadView() const { return bStageOverhead; }
	/** Flying to its own anchor on its chain (where this character is moved). */
	bool IsAnchorZipping() const { return bAnchorZipping; }
	/** Every machine: flying along its anchor's line now (the trail shows; it rams whoever it meets). */
	bool IsAnchorZipShown() const { return bAnchorZipShown; }
	/** Server: this character, flying on its anchor, rams Victim (1 damage, knocked along; once a flight). False if not now. */
	bool TryAnchorRam(AChaosImpactCharacter* Victim, bool bReported);
	/** How far past the flyer's body a ram reaches (cm). */
	static constexpr float AnchorRamReach = 40.0f;
	/** This flyer's ram reach now (雪玉アンカー: wider). */
	float GetAnchorRamReach() const;
	/** Where the chain of this character's hook starts on this screen (the right palm). False when not drawn. */
	bool GetHookHandLocation(FVector& OutLocation) const;
	/** Client: this player's own screen saw a hook catch it; the server checks and applies the hit. */
	UFUNCTION(Server, Reliable)
	void ServerReportHookHit(AChaosImpactHook* Hook);
	/** Client: this player's own screen was in a mine's blast; the server checks and applies it. */
	void ReportGadgetBlastFromClient(AChaosImpactGadget* Gadget) { ServerReportGadgetBlast(Gadget); }
	UFUNCTION(Server, Reliable)
	void ServerReportGadgetBlast(AChaosImpactGadget* Gadget);
	/** Server: this character's swap ball struck Other: the two change places (feet on each other's floor). False if not now. */
	bool SwapPlacesWith(AChaosImpactCharacter* Other);
	/** Server: how many times this character's swap balls have changed its place (the dojo counts them). */
	int32 GetSwapPlacesCount() const { return SwapPlacesCount; }
	/** Every machine: the blow of someone flying on an anchor meeting this character (its sound). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastAnchorRam(FVector_NetQuantize At);
	/** Every machine: the flash at both ends of a swap. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSwapShown(FVector_NetQuantize From, FVector_NetQuantize To);
	virtual bool IsMoveInputIgnored() const override;
	static constexpr float WindCarrySeconds = 1.1f;
	/** Client: this player's own screen touched a pickup; predicted now, confirmed by the server. */
	/** bContested: someone else was right by the ball on this screen (the server may give it to them). */
	void ClaimPickupFromClient(AChaosImpactBall* Ball, bool bContested = false);
	/** Client: hands over the locally predicted throw so the server's ball can continue from it. */
	AChaosImpactBall* TakePredictedThrowBall();
	/** Where this character is drawn this frame (network smoothing and latency lead included). */
	FVector GetPresentationLocation() const;
	bool IsDashingForPresentation() const { return bIsDashing || bReplicatedDashing; }
	/** Client: this screen reported a lethal hit that the server has not reflected yet. */
	bool IsEliminationPredicted() const;
	/** This player's round trip to the host in seconds; 0 for the host's own player and CPUs. */
	float GetNetworkRoundTripSeconds() const;
	/**
	 * Host, online: where this CPU was SecondsAgo (now when nothing that old is kept). A guest's screen shows the
	 * host's CPUs that much later, so their throws are also judged against where they saw them.
	 */
	FVector GetPastLocation(float SecondsAgo) const;
	/**
	 * How far back a remote player's throws are judged against CPUs: about as late as their screen shows them (the
	 * trip here plus the smoothing, less the lead they are drawn with); 0 for anyone else.
	 */
	static float GetRemoteViewDelay(const AChaosImpactCharacter* Thrower);
	/** VS team select, opening (until GO) and results: no moving, throwing, dashing or jumping. */
	virtual bool IsMatchInputLocked() const;
	/** Server: moves this character instantly (warp pads), on its own screen too when online, with the camera snapping along. */
	void WarpTo(const FVector& Location);

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball Inventory")
	int32 GetCarriedBallCount() const { return CarriedBallCount; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball Inventory")
	int32 GetMaximumCarriedBalls() const { return MaximumCarriedBalls; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Ball Inventory")
	bool HasVisibleHeldBall() const;

	/** Current remaining hit points. */
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Damage")
	float GetHealth() const { return Health; }

	/** Charge ratio in the range 0-1 while preparing a throw. */
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Throw")
	float GetThrowChargeAlpha() const;

	/** Horizontal direction currently used for aiming and throwing. */
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Aim")
	FVector GetAimDirection() const { return AimDirection; }

	/**
	 * Converts the raw engine-facing right-stick axes into this game's
	 * screen-space right/up convention. Public so the hardware convention is
	 * locked by an automation test instead of being changed by guesswork again.
	 */
	static FVector2D ConvertRawControllerAimAxes(const FVector2D& RawAxes);

	/** Current dodge stamina. One full point is consumed per dash. */
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Dash")
	float GetStamina() const { return Stamina; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Dash")
	bool IsDashing() const { return bIsDashing; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Dash")
	float GetDashDistance() const { return DashDistance; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Damage")
	float GetEliminationResetDelay() const { return EliminationResetDelay; }

	/** Read-only tuning used by the CPU to predict throws and dodges with the real rules. */
	float GetMaxHealth() const { return MaxHealth; }
	float GetMaxStamina() const { return MaxStamina; }
	float GetDashDuration() const { return DashDuration; }
	float GetMaxChargeSeconds() const { return MaxChargeSeconds; }
	float GetThrowReleaseDelay() const { return ThrowReleaseDelaySeconds; }
	float GetThrowSpeedForCharge(const float ChargeAlpha, const bool bArc) const
	{
		return FMath::Lerp(MinimumThrowSpeed, MaximumThrowSpeed, FMath::Clamp(ChargeAlpha, 0.0f, 1.0f))
			* (bArc ? ArcThrowSpeedScale : 1.0f);
	}
	/** True when StartDash would succeed right now (stamina, cooldown and state). */
	bool CanDashNow() const;

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: puts this character at a match start point with full health and no balls. */
	void ResetForOnlineMatch(const FVector& Location, const FRotator& Rotation);

	/** The nova parts carried (ChaosImpactNovaParts bits: gear, core, ring). */
	uint8 GetNovaParts() const { return NovaParts; }
	/**
	 * Server: a nova part picked up. With all three the nova is finished and goes into a ball slot (as soon as one is
	 * free); knocked out, the parts (and a finished nova) are dropped, never lost.
	 */
	void AddNovaPart(EChaosImpactNovaPart Part);

protected:
	// Online play: inputs are predicted locally and resolved by the server.
	UFUNCTION(Server, Reliable)
	void ServerStartCharge();
	UFUNCTION(Server, Reliable)
	void ServerReleaseThrow(float ChargeAlpha, FVector_NetQuantizeNormal Aim, FVector_NetQuantize ClientLocation,
		FVector_NetQuantize ClientNovaTarget, bool bHasClientNovaTarget, uint8 Sequence, bool bFused);
	/**
	 * The same throw sent beside it on the quick way (it may be lost; the reliable one above is resent, a round trip
	 * later on a lossy line). Whichever arrives first is the throw: this one only when it is the next in order and the
	 * hand here holds what the thrower's screen threw (ExpectedCount, ExpectedType), else the reliable one is waited for.
	 */
	UFUNCTION(Server, Unreliable)
	void ServerReleaseThrowEarly(float ChargeAlpha, FVector_NetQuantizeNormal Aim, FVector_NetQuantize ClientLocation,
		FVector_NetQuantize ClientNovaTarget, bool bHasClientNovaTarget, uint8 Sequence, uint8 ExpectedCount, uint8 ExpectedType,
		bool bFused);
	void HandleReleaseThrow(float ChargeAlpha, const FVector& Aim, const FVector& ClientLocation, const FVector& ClientNovaTarget,
		bool bHasClientNovaTarget, bool bFused);
	/** The owner's machine: the number of its last throw. The host: the last one carried out (0: none yet). */
	uint8 ThrowSequenceSent = 0;
	uint8 ThrowSequenceDone = 0;
	UFUNCTION(Server, Reliable)
	void ServerStartDash(FVector_NetQuantizeNormal Direction);
	UFUNCTION(Server, Unreliable)
	void ServerUpdateAim(FVector_NetQuantizeNormal Direction);
	/** The owning screen's free nova cursor, shown to every machine as the warning area. */
	UFUNCTION(Server, Unreliable)
	void ServerUpdateNovaTarget(FVector_NetQuantize Target);
	/** The owner's steering of its drive ball (flat; zero: straight on). */
	UFUNCTION(Server, Unreliable)
	void ServerDriveSteer(FVector_NetQuantizeNormal Direction);
	/** The owner let go of its drive ball (it dashed). */
	UFUNCTION(Server, Reliable)
	void ServerCancelDrive();
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayThrowAnimation();
	/** Every screen: a part picked up (Finished: the nova put together), with a flash and a sound. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastNovaPart(bool bFinished);
	/** Server: the nova put together once all three parts are carried and a ball slot is free. */
	void TryFinishNova();
	UPROPERTY(Replicated)
	uint8 NovaParts = 0;
	UFUNCTION(Client, Reliable)
	void ClientShowRespawn(const FString& DefeatedBy, float Seconds, APawn* KillerPawn);
	UFUNCTION(Client, Reliable)
	void ClientRespawned();
	UFUNCTION(Client, Reliable)
	void ClientShowKnockout(const FString& VictimName);
	UFUNCTION(Client, Unreliable)
	void ClientHitFeed(bool bDealt, const FString& Name);
	UFUNCTION(Server, Reliable)
	void ServerReportBallHit(AChaosImpactBall* Ball, FVector_NetQuantize HitLocation);
	/** The same report on the quick way (it may be lost): a ball's hit is counted once, whichever arrives first. */
	UFUNCTION(Server, Unreliable)
	void ServerReportBallHitEarly(AChaosImpactBall* Ball, FVector_NetQuantize HitLocation);
	void HandleReportedBallHit(AChaosImpactBall* Ball, const FVector& HitLocation);
	/** Server: the balls this player has reported being hit by (each once), and when. */
	TMap<TWeakObjectPtr<AChaosImpactBall>, double> HandledBallReports;
	/** Server moved this player (respawn, start point); the owner's trusted position must follow. */
	UFUNCTION(Client, Reliable)
	void ClientTeleportTo(FVector_NetQuantize Location, FRotator Rotation);
	UFUNCTION(Server, Reliable)
	void ServerClaimPickup(AChaosImpactBall* Ball);
	/** Answer to a pickup claimed from this player's screen; a refusal adopts the server's ball count. */
	UFUNCTION(Client, Reliable)
	void ClientPickupResolved(bool bAccepted, int32 ServerBallCount, uint16 ServerBallTypes, AChaosImpactBall* Ball);
	/** Answer to a throw released on this player's screen; a refusal also removes the preview. */
	UFUNCTION(Client, Reliable)
	void ClientThrowResolved(bool bAccepted, int32 ServerBallCount, uint16 ServerBallTypes);
	/** A server-side change of this player's ball count (elimination, respawn, match start). */
	UFUNCTION(Client, Reliable)
	void ClientBallCountReset(int32 ServerBallCount, uint16 ServerBallTypes);
	UFUNCTION(Server, Reliable)
	void ServerSwapBalls();
	/** The owner joined or parted its balls (for everyone else's screen; the throw itself says whether it was fused). */
	UFUNCTION(Server, Reliable)
	void ServerSetBallsFused(bool bFused);
	UFUNCTION()
	void OnRep_BallsFused();
	UFUNCTION(Server, Reliable)
	void ServerDropBall();
	/** A throw charge this player cancelled on their own screen. */
	UFUNCTION(Server, Reliable)
	void ServerCancelCharge();
	/** Answer to a swap made on this player's screen; resolves it in the same ordered queue as pickups and throws. */
	UFUNCTION(Client, Reliable)
	void ClientSwapResolved(int32 ServerBallCount, uint16 ServerBallTypes);
	/** Stamina is owned by each player's own machine; the server only sends changes it causes. */
	UFUNCTION(Client, Reliable)
	void ClientAddStamina(float Amount);
	UFUNCTION()
	void OnRep_CarriedBallCount();
	UFUNCTION()
	void OnRep_Eliminated();
	UFUNCTION()
	void OnRep_ReplicatedDashing();
	void PerformDash(const FVector& Direction);
	void ShowRespawnLocally(const FString& DefeatedBy, float Seconds, APawn* KillerPawn);
	void ApplyEliminatedPresentation(bool bNowEliminated);
	virtual void BeginPlay() override;
	float AppliedJumpTempo = 1.0f;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanJumpInternal_Implementation() const override;
	/** After an instant move: the camera jumps with the character instead of lagging across the stage. */
	void SnapCameraToCharacter();
	int32 CameraSnapFrames = 0;
	bool bCameraLagBeforeSnap = true;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for right-stick aiming input. Mouse aiming is read from the cursor. */
	void AimWithStick(const FInputActionValue& Value);
	void StopAimingWithStick(const FInputActionValue& Value);

	/** Starts and releases a charged throw. */
	void StartChargingThrow();
	void ReleaseChargedThrow();

	/** Updates aim from the mouse cursor or right stick. */
	void UpdateAim(float DeltaSeconds);
	FVector ApplyControllerAimAssist(const FVector& RawDirection) const;
	/**
	 * A player charging a throw (mouse or pad): the aim is drawn toward the nearest opponent near where it
	 * points, strongest as the charge starts and easing to a lighter hold.
	 */
	FVector ApplyChargeAimMagnet(const FVector& Direction) const;
	/**
	 * The opponent (or training target) nearest Direction within AngleDegrees, anywhere within reach: which way to throw
	 * to meet them (where a runner will be when the ball gets there), and how closely Direction already points at them.
	 */
	bool FindAimAssistTarget(const FVector& Direction, float AngleDegrees, FVector& OutToward, float& OutDot) const;
	/** A point in the world under the mouse (where to walk, where a nova lands): what the cursor is over. */
	bool FindMouseAimPoint(FVector& OutAimPoint) const;
	/**
	 * Which way to aim at the mouse: toward the point under the cursor in the aim arrow's own flat plane, so the arrow
	 * (and the throw, the dash, the tackle) points exactly at the cursor on screen.
	 */
	bool FindMouseAimDirection(FVector& OutDirection) const;
	/** The mouse's ray into the world, from the absolute pointer the painted reticle is drawn at. */
	bool DeprojectMouseRay(FVector& OutOrigin, FVector& OutDirection) const;
	void TryCreateChargeWidget();
	bool SpawnBall(float ChargeAlpha);
	void UpdateBallPresentation();
	void StartDash();
	void UpdateDash(float DeltaSeconds);
	void FinishDash();
	void CompleteAnimatedThrow();
	void PlayThrowAnimation();
	void RestoreLocomotionAnimation();
	void BeginRespawnCountdown();
	void ResetAfterElimination();
	/** A knocked-out CPU with bRespawnAfterElimination off leaves the game, with its controller. */
	void RemoveAfterElimination();
	/** Server, every few ticks: a character that has left the stage (a bug, a glitch through a wall) is knocked out. */
	void CheckLeftStage();
	void StartEliminationEffect();
	void UpdateEliminationEffect(float DeltaSeconds);
	void StopEliminationEffect();
	void StartRespawnEffect();
	void UpdateRespawnEffect(float DeltaSeconds);
	void UpdateAimGuidePresentation(bool bVisible);
	void UpdateDashTrailPresentation(bool bVisible);
	void BeginEliminationSpectate(APawn* KillerPawn);
	void EndEliminationSpectate();
	FString GetEliminatorDisplayName(AController* EventInstigator) const;

	/** Blueprint hooks for presentation/UI work without changing the C++ rules. */
	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Throw")
	void OnThrowChargeChanged(float ChargeAlpha);

	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Damage")
	void OnPlayerHit(float NewHealth, float DamageAmount);

	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Damage")
	void OnPlayerEliminated();

	/** Optional Blueprint hook for replacing or supplementing the built-in speed lines. */
	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Dash")
	void OnDashStarted(FVector Direction);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chaos Impact|Throw")
	TSubclassOf<AChaosImpactBall> BallClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="0.1"))
	float MaxChargeSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="1.0"))
	float MinimumThrowSpeed = 1450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="1.0"))
	float MaximumThrowSpeed = 3400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw")
	FVector ThrowSocketOffset = FVector(90.0f, 0.0f, 55.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="0.1", ClampMax="1.0"))
	float ArcThrowSpeedScale = 0.92f;

	/** Full-body fallback motion; replaceable from the character Blueprint later. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chaos Impact|Throw")
	TObjectPtr<UAnimSequenceBase> ThrowAnimation;

	/** Moment at which the real ball leaves the animated hand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ThrowReleaseDelaySeconds = 0.18f;
	/** Online: how far along its flight a remote player's late ball may leave on the host (see SpawnBall). */
	static constexpr float MaxThrowCatchUpSeconds = 0.3f;
	float PendingThrowCatchUp = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="0.1", ClampMax="4.0"))
	float ThrowAnimationPlayRate = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory", meta=(ClampMin="1", ClampMax="2"))
	int32 MaximumCarriedBalls = 2;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing=OnRep_CarriedBallCount, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory")
	int32 CarriedBallCount = 0;

	/** EChaosImpactBallType per carried slot, packed by ChaosImpactBallTypes::Pack; slot 0 is thrown first. */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing=OnRep_CarriedBallCount, Category="Chaos Impact|Ball Inventory")
	uint16 CarriedBallTypes = 0;

	/**
	 * 合体ボール: the two balls in hand are joined (slot 0 flies, slot 1 rides along). The owner's screen leads (its throw
	 * says whether it was fused); everyone else is told for the look in the hands.
	 */
	UPROPERTY(ReplicatedUsing=OnRep_BallsFused)
	bool bBallsFused = false;
	/** The two in hand, as they are, could be joined (wherever this is). */
	bool IsFusionPairHeld() const;
	/** Joins or parts them on this machine (and tells the server, from the owner's). bThrown: parted by the throw. */
	void SetBallsFused(bool bFused, bool bThrown);
	/** This machine's part: the sounds, the HUD's parting, the hands' look. */
	void OnBallsFusedChanged(bool bThrown);
	/** Each frame while joined: the left ball drawn into the right, the flash as they meet, then the fused look. */
	void UpdateFusionPresentation();
	double FusionJoinedAt = -100.0;
	bool bFusionFlashed = false;
	float FusedNovaScale = 0.0f;
	/** Server: a fused ノヴァ was being charged last frame (特異点ノヴァ's pull, 守護ノヴァ's walls). */
	bool bFusedNovaCharging = false;
	/** Server: a fused snowball rolled along carries what rides along (溶岩玉's fires, 霧の雪玉's smoke…). */
	ChaosImpactFusion::FFusionCarry RollCarry;
	double RollCarryFrom = 0.0;
	ChaosImpactFusion::FFusionLook HeldFusionLook;

	/** Set by the game mode when a CPU is spawned, so every machine can label it. */
	UPROPERTY(Replicated)
	uint8 CPUNumber = 0;

	void PushCarriedBall(EChaosImpactBallType Type);
	EChaosImpactBallType PopCarriedBall();
	void ClearCarriedBalls();
	void SwapCarriedBalls();
	/** Server: a knocked-out player's balls tumble out where they fell, loose for anyone to pick up. */
	void DropCarriedBalls();
	/** Server: the ball in hand rolls out onto the floor in front. */
	void DropFrontBall();
	/** Whether the ball in hand can be dropped now (not while it is being thrown). */
	bool CanDropBall() const;

	/** Snow: growth per carried slot (0-255 for 0-1), kept in step with the slots; the server grows it as its carrier walks. */
	UPROPERTY(Replicated)
	uint8 SnowGrowthRight = 0;

	UPROPERTY(Replicated)
	uint8 SnowGrowthLeft = 0;

	float SnowGrowthExact[2] = {0.0f, 0.0f};
	void SetSlotSnowGrowth(int32 Slot, float Growth);
	/** Server: grows carried snowballs by the distance walked; every machine: the weight slows its carrier. */
	void UpdateSnowball();
	FVector LastSnowWalkLocation = FVector::ZeroVector;
	bool bSnowWalkTracked = false;
	/** Last real travel direction used to detect quick left/right or forward/back lever-mashing. */
	FVector LastSnowMashDirection = FVector::ZeroVector;
	double NextSnowMashGrowthAt = 0.0;
	/** Walking speed with no snowball to carry (from the Blueprint, read at BeginPlay). */
	float BaseMaxWalkSpeed = 0.0f;
	/** The right-hand snowball is held up over the head (a spirit bomb), grown, slowly turning. */
	void UpdateSnowRollPresentation();
public:
	/**
	 * The right-hand snowball being rolled along the ground in front (not carried in the hand, not lifted for a throw):
	 * its centre and radius. bShown: where this screen draws its carrier (else where it really is).
	 */
	bool GetRolledSnowball(bool bShown, FVector& OutCenter, float& OutRadius) const;
	/** Whether Victim (standing where it is here) is under this character's rolled snowball. */
	bool IsUnderRolledSnowball(const AChaosImpactCharacter* Victim, bool bShown) const;
	/** Server: Victim is run over by this character's rolled snowball (1 damage, knocked away). False if not now. */
	bool TryRunOverWithSnowball(AChaosImpactCharacter* Victim, bool bReported);
	/** Rolling a snowball faster than this (cm/s) runs people over. */
	static constexpr float SnowRunOverMinSpeed = 140.0f;
	/** The same person is run over at most once in this long. */
	static constexpr float SnowRunOverCooldownSeconds = 1.0f;
	/** Running someone over breaks the snowball up on this many (one: the first run-over is its last). */
	static constexpr int32 SnowRunOverBreakHits = 1;
	/** How big (growth 0-1) a snowball can grow again after each run-over (none, one, two). */
	static float GetSnowGrowthCap(int32 Hits) { return Hits <= 0 ? 1.0f : Hits == 1 ? 0.66f : 0.33f; }
	/** Tests: a carried snowball's size, and how many have been run over with it. */
	void SetSnowGrowthForTests(const int32 Slot, const float Growth) { SetSlotSnowGrowth(Slot, Growth); }
	int32 GetSnowRunOverHits(const int32 Slot) const { return Slot >= 0 && Slot < 2 ? SnowRunOverHits[Slot] : 0; }
protected:
	/** Every machine: a rolled snowball runs opponents over (the host judges its own and CPUs, a guest its own player). */
	void UpdateSnowRunOver();
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, double> SnowRunOverAt;
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, double> SnowRunOverReportedAt;
	/** Server: how many each carried snowball has run over (it shrinks each time, breaking on the third). */
	int32 SnowRunOverHits[2] = {0, 0};
	/** Every screen: the snowball bursts where it ran someone over (bBroke: it broke up entirely). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSnowRunOver(FVector_NetQuantize At, float Scale, bool bBroke);
	float SnowRollBob = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SnowRollMesh;

	FQuat SnowRollSpin = FQuat::Identity;
	FVector LastSnowRollLocation = FVector::ZeroVector;

	/** Server time until which smoke fills this player's view (0 when clear). */
	UPROPERTY(Replicated)
	double BlindedUntilServerTime = 0.0;
	void ApplyHeldBallAppearance(UStaticMeshComponent* HandBall, EChaosImpactBallType Type);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldFireMaterial;
	/** A normal ball in the hand: a soft pale grey of its own (the bare sphere went near black on the shadowed side). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldNormalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldIceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldThunderMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldBlackMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldWindMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldSmokeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldBeamMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldSnowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldNovaMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldDriveMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldHookMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldAnchorMaterial;
	/** The newer kinds' (スワップ, バリア, マイン), made when first held. */
	UPROPERTY(Transient)
	TMap<EChaosImpactBallType, TObjectPtr<UMaterialInstanceDynamic>> HeldKindMaterials;
	int32 SwapPlacesCount = 0;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldPrisonMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> HeldPrisonCages[2];
	/** A prison ball's cage shows only on a hand that holds a prison ball (showing a hand's ball shows its children too). */
	void SyncHeldPrisonCages();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeldSimaeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RightHandBaseMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LeftHandBaseMesh;

	TWeakObjectPtr<AActor> LastDamageCauser;
	/** Who dealt the last hit (server; on a game for this machine alone, read by the hit stop). */
	TWeakObjectPtr<APawn> LastHitSourcePawn;
	double LastDamagedAt = -100.0;

	/** Server time a throw started charging (-1: not charging), so other machines can show it. */
	UPROPERTY(Replicated)
	float ChargeStartServerTime = -1.0f;

	UFUNCTION(Client, Reliable)
	void ClientBlastKnockback(FVector_NetQuantize Velocity);


	/**
	 * Nova charge: the nova over the head swelling with the charge, energy streaming into it from all around, an
	 * aura rising at the feet, and this player's camera drawn back to take it all in.
	 */
	void UpdateNovaChargePresentation(float DeltaSeconds);
	/** Local-only free cursor and whole-stage camera target while a nova is charging. */
	void UpdateNovaTargeting(float DeltaSeconds);
	bool ResolveNovaTargetPoint(const FVector& Candidate, FVector& OutTarget) const;
	void GetNovaStageView(FVector& OutCenter, FVector2D& OutHalfExtent) const;
	ChaosImpactBallTypes::FNovaLook HeldNovaLook;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> NovaAnchor;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> NovaAura;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> NovaMoteMaterial;

	struct FNovaMote
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FVector Start = FVector::ZeroVector;
		double StartedAt = 0.0;
		float Seconds = 1.0f;
		bool bFlying = false;
	};
	TArray<FNovaMote> NovaMotes;
	bool bNovaChargeShown = false;
	/** Exact ground point selected by a human nova thrower. Replicated so every screen sees the same warning. */
	UPROPERTY(Replicated)
	FVector_NetQuantize NovaTargetLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	bool bNovaTargetValid = false;

	double NextNovaTargetSendAt = 0.0;
	/** This player's camera drawn back, and moved toward where the nova will land, until a while after the throw. */
	float NovaCameraExtra = 0.0f;
	bool bStageOverhead = false;
	FVector StageOverheadCenter = FVector::ZeroVector;
	float StageOverheadHalf = 2400.0f;
	FVector NovaCameraLead = FVector::ZeroVector;
	double NovaCameraHoldUntil = 0.0;
	FRotator SavedCameraBoomRotation = FRotator(-60.0f, 0.0f, 0.0f);
	float AppliedDuelCameraYawOffset = 0.f;

	/** Whole-stage nova targeting camera and right-stick cursor tuning. */
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Nova Targeting", meta=(ClampMin="1.0"))
	float NovaOverviewHeightScale = 1.75f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Nova Targeting", meta=(ClampMin="0.0"))
	float NovaOverviewPadding = 350.0f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Nova Targeting", meta=(ClampMin="100.0"))
	float NovaTargetCursorSpeed = 1500.0f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Nova Targeting", meta=(ClampMin="0.1"))
	float NovaOverviewBlendSpeed = 5.0f;
	/** Where the nova being charged was last shown to land (valid while LandingPreviewRadius > 0). */
	FVector LandingPreviewGround = FVector::ZeroVector;

	/**
	 * Where a nova being charged will land, drawn on the ground for everyone to see: its whole blast, to run from.
	 */
	void UpdateLandingPreview(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> LandingRing;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LandingFill;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LandingRingMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LandingFillMaterial;

	EChaosImpactBallType LandingPreviewType = EChaosImpactBallType::Normal;
	float LandingPreviewRadius = 0.0f;

	/**
	 * Charging a barrier ball, for this player only: its flight as a dashed line and the wall as it will stand where it
	 * comes down (the throw puts it just there).
	 */
	void UpdateBarrierPreview();
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> BarrierPreviewLines;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> BarrierPreviewPane;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BarrierPreviewLineMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BarrierPreviewPaneMaterial;
	/** The top-down camera moves over toward where the barrier will stand, so it is in view. */
	FVector BarrierCameraLead = FVector::ZeroVector;
	/** Server: where a guest's own preview had its barrier (sent with the throw). */
	FVector PendingBarrierClientFloor = FVector::ZeroVector;
	bool bPendingBarrierClientFloor = false;

	/** Both arms raised over the head (charging a nova, lifting a big snowball), 0-1. */
	float ArmsRaisedWeight = 0.0f;
	/** Drive ball poses: cupped in both hands while charged, then the arm reaching after it (see the puppet). */
	float DriveHoldWeight = 0.0f;
	float DrivePointWeight = 0.0f;
	/** Where the guiding arm reaches, trailing the ball a little. */
	FVector DrivePointShown = FVector::ZeroVector;
	/** The ball type last charged, so another screen's throw motion knows a drive ball was thrown. */
	EChaosImpactBallType LastChargedType = EChaosImpactBallType::Normal;
	bool bDriveThrowAnimation = false;
	/** Another screen's drive ball this character steers (found by looking, as only its thrower knows it). */
	TWeakObjectPtr<AChaosImpactBall> ShownDriveBall;
	double NextDriveBallSearchAt = 0.0;
	const AChaosImpactBall* FindShownDriveBall();
	/** Where a drive ball is held, cupped in both hands, and pushed out from: in front of the chest (actor space). */
	FVector DriveHoldOffset = FVector(58.0f, 0.0f, 22.0f);
	/** A big snowball being lifted from the ground up over the head, 0-1. */
	float SnowLift = 0.0f;

	/**
	 * Sounds from what changed since last frame (a dash begun, a jump, a landing, a ball picked up or thrown, a hit),
	 * on every screen alike, whoever drives this character.
	 */
	void UpdateSoundPresentation();
	bool bSoundWasDashing = false;
	bool bSoundWasFalling = false;
	bool bSoundWasThrowing = false;
	bool bSoundChargeFull = false;
	float SoundFallSpeed = 0.0f;
	int32 SoundBallCount = -1;
	uint16 SoundBallTypes = 0;
	float SoundHealth = -1.0f;
	/** A nova swelling up over the head hums as it grows. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> NovaChargeSound;

	void UpdateCameraShake();
	/**
	 * Game feel, drawn on each machine: the camera widening for a moment at a dash or a hard throw and running a
	 * little ahead, the body leaning into starts, stops and turns, a throw's whoosh.
	 */
	void UpdateFeelPresentation(float DeltaSeconds);
	/** A hit or a knockout felt here: the world held for a beat (hit stop), the shake for nearby players, a thud. */
	void PlayHitFeel(bool bKnockout);
	float FovKick = 0.0f;
	float FovKickVelocity = 0.0f;
	/** Over the shoulder, charging: the view narrows a little to aim (sprung in and out). */
	float ShoulderFovOffset = 0.0f;
	float ShoulderFovVelocity = 0.0f;
	float BaseFieldOfView = -1.0f;
	/** The charge of this screen's own last throw (how hard the camera kicks and the throw whooshes). */
	float FeelThrowCharge = 0.0f;
	/** Soft FX-kit pieces for a hit's burst. */
	TWeakObjectPtr<class AChaosImpactFxKit> FeelFx;
	class AChaosImpactFxKit* GetFeelFx();
	/** A dash's afterimages: see-through copies of the body, each in the pose it was left in, fading out. */
	struct FDashGhost
	{
		TArray<TWeakObjectPtr<UMeshComponent>> Parts;
		TWeakObjectPtr<UMaterialInstanceDynamic> Look;
		float Age = 0.0f;
	};
	TArray<FDashGhost> DashGhosts;
	float GhostOwed = 0.0f;
	bool bGhostDashing = false;
	void SpawnDashGhost();
	void UpdateDashGhosts(float DeltaSeconds, bool bDashing);
	FVector CameraMoveLead = FVector::ZeroVector;
	FVector DojoCameraLead = FVector::ZeroVector;
	bool bDojoRooted = false;
	FVector FeelLastLocation = FVector::ZeroVector;
	FVector FeelVelocity = FVector::ZeroVector;
	FVector FeelAcceleration = FVector::ZeroVector;
	FQuat MeshLeanBase = FQuat::Identity;
	FVector2D MeshLean = FVector2D::ZeroVector;
	FVector2D MeshLeanVelocity = FVector2D::ZeroVector;
	bool bMeshLeanBaseSet = false;
	bool bMeshLeaning = false;
	bool bAnimHeld = false;
	bool bFeelWasDashing = false;
	bool bFeelWasThrowing = false;
	double AnimHeldUntil = 0.0;
	float CameraShakeStrength = 0.0f;
	float CameraShakeSeconds = 1.0f;
	double CameraShakeStartedAt = -100.0;
	bool bCameraShaking = false;
	FVector CameraRestLocation = FVector::ZeroVector;

	/** Ball shown on the right hand while at least one ball is carried. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> HeldBallMesh;

	/** Second carried ball, shown in the left hand only while inventory is full. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> LeftHeldBallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory")
	FVector HeldBallRelativeLocation = FVector(0.0f, 0.0f, 2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory")
	FRotator HeldBallRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory")
	FVector LeftHeldBallRelativeLocation = FVector(0.0f, 0.0f, 2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory")
	FRotator LeftHeldBallRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="30.0"))
	float StickAimDeadZone = 0.22f;

	/** A second radial guard prevents editor/plugin stick drift from becoming movement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Movement", meta=(ClampMin="0.0", ClampMax="0.95"))
	float MovementStickDeadZone = 0.28f;

	/** Target attraction for stick aiming: the opponent nearest the aim, anywhere on the stage, draws it toward them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="45.0"))
	float ControllerAimAssistAngleDegrees = 25.0f;

	/** How far off an opponent can be and still draw the aim (the whole stage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0"))
	float ControllerAimAssistDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ControllerAimAssistStrength = 0.55f;

	/** While charging a throw: opponents within this angle of the aim draw it toward them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="60.0"))
	float ChargeAimMagnetAngleDegrees = 35.0f;

	/** How far toward that opponent the aim is drawn as the charge starts (1 = right onto them)... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ChargeAimMagnetStartStrength = 0.8f;

	/** ...and after ChargeAimMagnetEaseSeconds of charging, for the rest of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ChargeAimMagnetHoldStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.01"))
	float ChargeAimMagnetEaseSeconds = 0.6f;

	/** Length of the temporary aiming arrow drawn while charging. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0"))
	float AimGuideLength = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Damage", meta=(ClampMin="1.0"))
	float MaxHealth = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Damage", meta=(ClampMin="0.0"))
	float EliminationResetDelay = 3.0f;

	/** Training and the online lobby (no VS stage): falling this far below the start point knocks the character out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Damage", meta=(ClampMin="100.0"))
	float TrainingFallKnockoutDepth = 2000.0f;

	/** Briefly keeps the defeated player's original camera before spectating. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Damage", meta=(ClampMin="0.0", ClampMax="2.0"))
	float EliminationCameraHoldSeconds = 0.45f;

	UPROPERTY(VisibleInstanceOnly, Replicated, BlueprintReadOnly, Category="Chaos Impact|Damage")
	float Health = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="1.0"))
	float MaxStamina = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="0.0"))
	float StaminaRegenPerSecond = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="0.0"))
	float StaminaRecoveredPerBallHit = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="0.0"))
	float DashCost = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="1.0"))
	float DashDistance = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="0.01"))
	float DashDuration = 0.14f;

	/** Delay after a dash finishes before another dash can begin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Dash", meta=(ClampMin="0.0"))
	float DashCooldownSeconds = 0.8f;

	/** Owned by each player's own machine online; the server only sends the changes it causes. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Dash")
	float Stamina = 5.0f;

	/** Other machines only draw the dash trail; the dash itself runs on the owner and server. */
	UPROPERTY(ReplicatedUsing=OnRep_ReplicatedDashing)
	bool bReplicatedDashing = false;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal ReplicatedDashDirection = FVector::ForwardVector;

	UPROPERTY(ReplicatedUsing=OnRep_Eliminated)
	bool bEliminated = false;

	/** Server time until which this player is encased in ice (0 when not frozen). */
	UPROPERTY(Replicated)
	double IceFrozenUntilServerTime = 0.0;

	double GetSharedServerTime() const;

	/** The drive ball being steered (this machine's: a throw preview until the server's ball takes over). */
	TWeakObjectPtr<AChaosImpactBall> DrivenBall;
	/** Steering ends then (shared server time; negative: not steering). */
	double DriveEndsAt = -1.0;
	/** The same, for other screens (their pose). */
	UPROPERTY(Replicated)
	float DriveEndsAtServerTime = -1.0f;
	FVector DriveSteer = FVector::ZeroVector;
	double DriveSteerAt = 0.0;
	FVector LastSentDriveSteer = FVector::ZeroVector;
	double NextDriveSendAt = 0.0;
	void BeginDriving(AChaosImpactBall* Ball);
	void UpdateDriving(float DeltaSeconds);
	/** bLetGo: the ball fizzles out (dashed away, hit); otherwise it already burst or its time ran out. */
	void EndDriving(bool bLetGo);

	/** Freeze transitions and the slide on frozen ground; movement changes only where this player is moved. */
	void UpdateIceStatus(float DeltaSeconds);
	/** Development (-CINetTrace): logs where this machine draws the character, for measuring online lag. */
	void TraceNetPresentation(float DeltaSeconds);
	float NetTraceSeconds = 0.0f;
	/** Black holes draw this character in; applied where it is moved, like the slide on ice. */
	void UpdateBlackHolePull(float DeltaSeconds);
	/** A tornado's blow (StartWindKnockback), applied where this character is moved. */
	void UpdateWindKnockback(float DeltaSeconds);
	void UpdateWindCarry(float DeltaSeconds);
	/** Reeled in by a hook: moved toward its thrower where this character is moved; ends on arrival, a dash, or time. */
	void UpdateHookPull(float DeltaSeconds);
	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactHook> HookPuller;
	float HookPullSeconds = 0.0f;
	/** This character's own hook while it is out, and the arms' hook poses (spinning it overhead, on the chain, reeling). */
	TWeakObjectPtr<AChaosImpactHook> ShownHook;
	/** Its own anchor's flight: the anchor, from where, how long so far and in all; the last one flown (not again). */
	TWeakObjectPtr<AChaosImpactHook> ZipHook;
	/** 光の跳躍: this flight goes through a wall. */
	bool bZipThrough = false;
	TWeakObjectPtr<AChaosImpactHook> ZipDoneHook;
	bool bAnchorZipping = false;
	bool bZipToWall = false;
	/** How its capsule met other characters before the flight (it slips past them on the way). */
	TEnumAsByte<ECollisionResponse> ZipPawnResponse = ECR_Block;
	FVector ZipFrom = FVector::ZeroVector;
	FVector ZipTo = FVector::ZeroVector;
	float ZipSeconds = 0.0f;
	float ZipDuration = 0.0f;
	void UpdateAnchorZip(float DeltaSeconds);
	void EndAnchorZip(bool bArrived);
	/** Flying on its anchor, as every machine sees (the flyer's machine moves it and says so through the host). */
	UPROPERTY(Replicated)
	bool bAnchorZipShown = false;
	void SetAnchorZipShown(bool bOn);
	UFUNCTION(Server, Reliable)
	void ServerSetAnchorZipShown(bool bOn);
	/** Client: this screen's player was rammed by Attacker flying on its anchor; the host checks it. */
	UFUNCTION(Server, Reliable)
	void ServerReportAnchorRam(AChaosImpactCharacter* Attacker);
	/** Every machine: the host rams the people it judges, a guest reports its own player, every screen draws the trail. */
	void UpdateAnchorRam(float DeltaSeconds);
	void UpdateAnchorZipPresentation();
	/** Server: who this flight has rammed already, where it was last checked, when it ended (late reports). */
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> AnchorRamHit;
	FVector AnchorRamLast = FVector::ZeroVector;
	bool bAnchorRamTracked = false;
	double AnchorZipEndedAt = -100.0;
	/** A guest's own player: the flyers it has reported ramming it (until they land). */
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> AnchorRamReported;
	/** The trail's effect, and when its next pieces are due. */
	TWeakObjectPtr<class AChaosImpactFxKit> ZipFx;
	double ZipFxNextAt = 0.0;
	bool bZipFxWasOn = false;
	float HookSpinWeight = 0.0f;
	float HookSpinAngle = 0.0f;
	float HookChainWeight = 0.0f;
	float HookReelWeight = 0.0f;
	float HookReelTime = 0.0f;
	FVector HookAimShown = FVector::ZeroVector;
	UPROPERTY(Replicated)
	TObjectPtr<AChaosImpactTornado> WindCarrier;
	UPROPERTY(Replicated)
	double WindCarryStartServerTime = 0.0;
	float WindCarryAngle = 0.0f;
	float WindCarryRadius = 0.0f;
	bool bWindCarryPosed = false;
	FVector MeshRestLocation = FVector::ZeroVector;
	FRotator MeshRestRotation = FRotator::ZeroRotator;
	FVector WindKnockbackVelocity = FVector::ZeroVector;
	double WindKnockbackStartedAt = -100.0;
	float WindKnockbackSeconds = 0.0f;
	/**
	 * Client: keeps this frame's direct move (a dash step, a black hole's pull) from being erased. A client folds
	 * similar moves into one before sending them and rewinds to the first move's start to do it.
	 */
	void PreventClientMoveCombining();
	void SetIceFreezePresentation(bool bFrozen);
	void UpdateIceFreezePresentation(float DeltaSeconds);
	bool bIceFreezeActive = false;
	bool bIceThawing = false;
	float IceFreezeVisualSeconds = 0.0f;
	bool bOnSlipperyIce = false;
	float DefaultGroundFriction = 100.0f;
	float DefaultBrakingFriction = 4.0f;
	float DefaultBrakingDecelerationWalking = 100000.0f;
	float DefaultMaxAcceleration = 100000.0f;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> IceBlockMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> IceShardMeshes;

	/** Flames on a carried fire ball, one per hand. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> RightHeldFire;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> LeftHeldFire;

	/** Which ball type each hand's effect above was made for (fire, thunder or black). */
	EChaosImpactBallType RightHeldEffectType = EChaosImpactBallType::Normal;
	EChaosImpactBallType LeftHeldEffectType = EChaosImpactBallType::Normal;

	TArray<FTransform> IceShardTransforms;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> IceOverlayMaterial;

	/**
	 * Down to the last hit (training, the room lobby, or a VS match in play): a small puff of smoke leaves the
	 * body now and then, a quiet hint opponents can spot. Presentation only, on every machine.
	 */
	void UpdateLastHitPresentation();
	bool bLastHitSmokeOn = false;
	double NextLastHitPuffAt = 0.0;
	double LastHitPuffEndsAt = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> LastHitSmoke;

	/**
	 * Draws the player with the character model in Content/ChaosImpact/Character, coloured by team, instead of the
	 * template mannequin. The mannequin stays (hidden) and keeps animating; the model copies its pose.
	 */
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Appearance")
	bool bUseToonCharacter = true;

	/** Off: walking over balls never picks them up (a boss that fights with its own moves leaves them to the player). */
	bool bCanCollectBalls = true;

	/** Size of that model against its source file; 1.45 makes it about as tall as the template mannequin. */
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Appearance", meta=(ClampMin="0.5", ClampMax="3.0"))
	float ToonCharacterScale = 1.45f;

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactPuppetComponent> ToonCharacter;

	void CreateToonCharacter();
	/** Team colour, and the throwing hand following the mannequin's while the thrown ball is still in it. */
	void UpdateToonCharacter();
	double ThrowAnimationStartedAt = -100.0;

	/** Rumble for what just happened to the player on this machine: hits, knockouts, a full charge, pickups, pulls. */
	void UpdateControllerRumble();
	float RumbleLastHealth = -1.0f;
	bool bRumbleWasEliminated = false;
	bool bRumbleChargeFull = false;
	int32 RumbleLastBallCount = -1;
	/** Set by UpdateBlackHolePull on the frames a black hole moved this character. */
	bool bRumbleBlackHolePull = false;

	double NextAimSendAt = 0.0;
	FVector LastSentAim = FVector::ZeroVector;

	/**
	 * Online movement: a remote player's client position is trusted (no rubber-banding), except for a
	 * short window after the server teleports them and while they are eliminated.
	 */
	void UpdateMovementAuthority();
	void NotifyServerTeleport();
	static constexpr float ServerTeleportAuthoritySeconds = 0.75f;
	float ServerAuthoritativeUntil = 0.0f;
	/** Set while applying a client-reported hit, whose dash invulnerability was already checked by the owner. */
	bool bApplyingReportedHit = false;
	bool bApplyingRoyaleElimination = false;
	/** The owner already started the throw motion locally; skip the echo from the server. */
	bool bPredictedThrowAnimation = false;
	/** Client: the cosmetic ball thrown on this screen before the server's ball arrives. */
	TWeakObjectPtr<AChaosImpactBall> PredictedThrowBall;
	double PredictedThrowSpawnedAt = 0.0;
	/** Server: how far ahead the remote thrower's own screen had them when they released. */
	FVector ThrowOriginOffset = FVector::ZeroVector;
	/** Development: -CIAutoInput lets an online client play by itself for latency testing. */
	void TickDevAutoInput();
	bool bDevAutoInput = false;
	/** Development (-CIAutoInputHost): the host's own player plays by itself too (for online tests). */
	bool bDevAutoInputHost = false;
	/**
	 * Development (-CILagProbe): online positions and throws in the log, by the server's clock, so each player's own
	 * screen can be compared with the host's copy of them ("LAGPROBE ...").
	 */
	void LogLagProbe(const TCHAR* Event = nullptr);
	double NextLagProbeAt = 0.0;
	double DevNextThrowAt = 0.0;
	double DevReleaseAt = 0.0;
	double DevNextDashAt = 0.0;
	/** Development auto input: walking into a wall, it goes round for a moment. */
	double DevSlowSince = -1.0;
	double DevDetourUntil = 0.0;
	FVector DevDetourDirection = FVector::ZeroVector;

	/**
	 * Online: another player's copy trails their own screen by their round trip plus smoothing.
	 * The mesh is drawn that far ahead along their velocity; the capsule and gameplay are untouched.
	 */
	void UpdatePresentationLead(float DeltaSeconds);
	static constexpr float MaxPresentationLeadSeconds = 0.4f;
	/** ...and for a slow line (a round trip of a second): led by up to this long and this far. */
	static constexpr float SlowLinePresentationLeadSeconds = 0.65f;
	static constexpr float SlowLinePresentationLeadDistance = 420.0f;
	/** How much of a remote player's round trip their drawn body is led ahead by. */
	static constexpr float PresentationLeadShareOfPing = 0.3f;
	/** Host, online: a CPU's own recent path (GetPastLocation). */
	TArray<TPair<double, FVector>> PastLocations;
	/** カスタムカラー now on the model (the player state's, unless a CPU or a team battle). */
	TArray<uint8> ShownCustomColours;
	/** ペイント now on the model (the player state's key, unless hidden by the settings) and the model it is on. */
	int32 ShownPaintKey = 0;
	TWeakObjectPtr<UChaosImpactPuppetComponent> ShownPaintOn;
	static constexpr float MaxPresentationLeadDistance = 260.0f;
	/**
	 * Online, a rough line: the news of where another player is comes in bunches (nothing for a third of a second, then
	 * several at once), which showed as stopping and lurching. Each position is kept with the time it was true at its
	 * sender (the player's own move time on the host; the host's time on a guest), and the body is drawn from them a
	 * little behind, by as much as the line has been late lately: evenly, as it really moved. A steady line is not held
	 * back at all.
	 */
	struct FNetPathSample
	{
		double SourceTime = 0.0;
		FVector Location = FVector::ZeroVector;
	};
	TArray<FNetPathSample> NetPath;
	double NetPathLastSource = -1.0;
	/** This machine's clock less the sender's, at the quickest delivery seen (the line's steady delay). */
	double NetPathClockOffset = 0.0;
	bool bNetPathClockValid = false;
	/** How late the news has been running lately beyond that (falls back slowly), and how far behind it is drawn now. */
	float NetPathLateness = 0.0f;
	float NetPathDelay = 0.0f;
	FVector PresentationBufferWorld = FVector::ZeroVector;
	void UpdateNetPathBuffer(float DeltaSeconds, bool bDrawnFromNetwork);
	static constexpr float MaxNetPathDelay = 0.35f;
	/** Host: its clock when this character last moved, sent with its position (a guest's buffer orders by it). */
	UPROPERTY(Replicated)
	float NetPathServerTime = 0.0f;
	static constexpr float PresentationLeadBlendSpeed = 9.0f;
	/** The fastest the lead may swing round (cm per second). */
	static constexpr float MaxPresentationLeadChangeSpeed = 500.0f;
	/** How fast an uneven step is let out (per second). */
	static constexpr float PresentationGlideRate = 10.0f;
	FVector CachedBaseTranslationOffset = FVector::ZeroVector;
	FVector PresentationLeadWorld = FVector::ZeroVector;
	/**
	 * A remote copy's position arrives in uneven steps (bunched and spaced out updates at a high ping). The drawn
	 * body keeps going at the copy's own speed and only glides onto where the steps put it (world space).
	 */
	FVector PresentationGlideWorld = FVector::ZeroVector;
	FVector PresentationLastActorLocation = FVector::ZeroVector;
	bool bPresentationHasLast = false;

	/** Client: health expected after this screen's own hit reports, valid until PredictedHealthUntil. */
	float PredictedHealth = 0.0f;
	double PredictedHealthUntil = 0.0;
	/**
	 * Client ball count reconciliation. Pickups (+1) and throws (-1) sent to the server, oldest first,
	 * wait here for their answer. Answers and server events arrive in the same ordered reliable stream,
	 * so the drawn count is always: last answered server count + the changes still waiting.
	 */
	TArray<int8> PendingBallActions;
	int32 ServerAnsweredBallCount = 0;
	uint16 ServerAnsweredBallTypes = 0;
	/** Pickups are stored as 1 + EChaosImpactBallType, throws as -1, swaps of the two hands as -2. */
	void RefreshPredictedBallCount();
	void ResolveOldestBallAction(int32 ServerBallCount, uint16 ServerBallTypes);
	int32 GetPendingPickupCount() const;
	/** Client: for each pickup still waiting for its answer (oldest first), whether it was contested. */
	TArray<bool> PendingPickupContested;
	/** Client: a release waiting for the pickup it depends on to be confirmed. */
	bool bThrowAwaitingPickup = false;
	float AwaitingThrowChargeAlpha = 0.0f;
	void ReleaseThrowNow(float ChargeAlpha);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Aim")
	FVector AimDirection = FVector::ForwardVector;

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactChargeWidget> ChargeWidget;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Damage")
	TArray<TObjectPtr<UStaticMeshComponent>> EliminationPieces;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Damage")
	TObjectPtr<UPointLightComponent> EliminationFlash;

	/** Real world-space guide pieces render correctly in every split-screen view. */
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Aim")
	TArray<TObjectPtr<UStaticMeshComponent>> AimGuidePieces;

	/** Real world-space speed lines render correctly in every split-screen view. */
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Dash")
	TArray<TObjectPtr<UStaticMeshComponent>> DashTrailPieces;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Damage", meta=(ClampMin="0.1"))
	float EliminationEffectDuration = 0.85f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Damage", meta=(ClampMin="0.1"))
	float RespawnEffectDuration = 0.65f;

	TArray<FVector> EliminationPieceDirections;
	float EliminationEffectTime = 0.0f;
	bool bEliminationEffectActive = false;
	float RespawnEffectTime = 0.0f;
	float RespawnAtWorldSeconds = 0.0f;
	bool bRespawnEffectActive = false;
	FVector InitialMeshRelativeScale = FVector::OneVector;
	TWeakObjectPtr<AActor> EliminationViewTarget;
	TWeakObjectPtr<AController> EliminationInstigator;
	/** This knockout was for leaving the stage, not a hit: the respawn panel says so instead of naming someone. */
	bool bKnockedOutLeavingStage = false;
	double NextLeftStageCheckAt = 0.0;
	TWeakObjectPtr<AChaosImpactBall> PendingThrowBall;
	TSubclassOf<UAnimInstance> LocomotionAnimInstanceClass;
	FVector PendingThrowDirection = FVector::ForwardVector;
	float PendingThrowSpeed = 0.0f;
	float PendingThrowArcUpwardSpeed = 0.0f;
	EChaosImpactBallFlightMode PendingThrowFlightMode;
	FVector PendingNovaTarget = FVector::ZeroVector;
	bool bPendingNovaTarget = false;
	FTimerHandle ThrowReleaseTimer;
	FTimerHandle ThrowAnimationResetTimer;
	FTimerHandle EliminationCameraHoldTimer;
	FTimerHandle RespawnTimer;

	FVector2D StickAimInput = FVector2D::ZeroVector;
	FVector LastMoveDirection = FVector::ForwardVector;
	FVector DashDirection = FVector::ForwardVector;
	FVector InitialSpawnLocation = FVector::ZeroVector;
	FRotator InitialSpawnRotation = FRotator::ZeroRotator;
	float ThrowChargeStartedAt = 0.0f;
	float DashElapsedSeconds = 0.0f;
	/** World time the last dash ended; a black hole does not pull for a moment after. */
	double DashEndedAtSeconds = -100.0;
	/** Where the current dash began; development logging (-CIDashLog) reports how far it really went. */
	FVector DashStartLocation = FVector::ZeroVector;
	float DashDistanceApplied = 0.0f;
	float NextDashAvailableAtSeconds = 0.0f;
	bool bIsChargingThrow = false;
	bool bThrowReleasePending = false;
	bool bThrowAnimationActive = false;
	bool bIsDashing = false;
	bool bWasFallingBeforeDash = false;
	/** A charge started by the throw control (and still held) / the throw control was down last frame. */
	bool bMouseChargeActive = false;
	bool bWasMouseDownLastTick = false;
	/** Each bound control's state last frame, to act once per press. */
	bool BoundActionHeld[32] = {};
	/** Reads the player's own controls (the settings screen's) and acts on them; every frame for a local player. */
	void UpdateBoundControls();
	/** スマホ版: the fingers this frame, acted on (false when this player does not play by touch). */
	bool UpdateTouchControls(const APlayerController* PlayerController, double Now);
	/** Who the right thumb's tap or undragged charge throws at: the nearest opponent (else ahead). */
	FVector GetTouchAutoAim() const;
	FChaosImpactTouchGestures TouchGestures;
	FChaosImpactTouchFrame LastTouchFrame;
	bool bTouchAimActive = false;
	bool bTouchAimDragged = false;
	FVector TouchAimWorld = FVector::ForwardVector;
	/**
	 * スマホ版: the way the 向き stick last turned this player, kept after it is let go (walking then sidesteps instead of
	 * turning round) until the stick turns them again or a throw goes another way. Not set: faced the way they walk.
	 */
	bool bTouchFacingHeld = false;
	FVector TouchHeldFacing = FVector::ForwardVector;
	bool bTouchQuickRelease = false;
	/**
	 * 片手モード: the thumb off and standing still, the ball charges by itself (bOneHandCharging) at whoever is near and
	 * goes when full (or when the thumb comes back: that tap then throws nothing more).
	 */
	void UpdateOneHandThrow(const struct FChaosImpactTouchFrame& Frame, double Now);
	/** Whoever is nearest within Reach (bLead: where they will be when the ball gets there): the way to them. */
	bool FindTouchTarget(FVector& OutWay, float Reach, bool bLead) const;
	bool bOneHandCharging = false;
	bool bOneHandSkipTap = false;
	bool bOneHandThumbWasDown = false;
	double OneHandStillSince = 0.0;
	double OneHandFullAt = -1.0;
	static constexpr float OneHandReach = 2600.0f;
	static constexpr double OneHandChargeDelay = 0.3;
	double TouchJumpUntil = -1.0;
	bool bTouchForTests = false;
	TArray<FVector2D> TouchForTests;
	void UpdateClickMovement();
	void UpdateHeldMouseMovement();
	void RequestDirectionalDash(const FVector& Direction);
	ChaosImpactEasyControls::FManageGesture BallManageGesture;
	ChaosImpactEasyControls::FManageGesture PointerDropGesture;
	ChaosImpactEasyControls::FMoveClicks MoveClickGesture;
	uint8 LastControlScheme = 255;
	bool bSuppressPointerThrow = false;
	bool bWaitForThrowRelease = false;
	bool bPointerDropActive = false;
	bool bPointerMoveHeld = false;
	bool bClickMoveActive = false;
	bool bClickMoveRejected = false;
	FVector ClickMoveGoal = FVector::ZeroVector;
	TArray<FVector> ClickMovePath;
	int32 ClickMoveWaypoint = 0;
	FVector ClickLastProgress = FVector::ZeroVector;
	double ClickProgressAt = 0.0;
	bool bTrainingMenuFrozen = false;
	bool bMapWalker = false;
	/** スマホ版の world map: a finger dragged on the left of the screen walks (a stick where it went down). */
	bool bMapDragDown = false;
	FVector2D MapDragOrigin = FVector2D::ZeroVector;
	bool bExploration = false;
	float ExplorationSpeed = 1.0f;
	bool bTrainingMenuCameraActive = false;
	uint8 SavedTrainingMenuMovementMode = 1;
	uint8 SavedTrainingMenuCustomMovementMode = 0;
	float SavedCameraArmLength = 800.0f;
	float StageCameraScale = 1.0f;
	FVector SavedCameraSocketOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Camera", meta=(ClampMin="250.0", ClampMax="800.0"))
	float TrainingMenuCameraArmLength = 490.0f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Camera")
	FVector TrainingMenuCameraSocketOffset = FVector(0.0f, -190.0f, 30.0f);

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Camera", meta=(ClampMin="1.0"))
	float TrainingMenuCameraBlendSpeed = 7.5f;
	/** The camera boom's spring speeds (its length, offsets and turn ease off and into place). */
	float CameraArmVelocity = 0.0f;
	FVector CameraSocketVelocity = FVector::ZeroVector;
	FVector CameraTargetVelocity = FVector::ZeroVector;
	FVector CameraTurnVelocity = FVector::ZeroVector;

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};


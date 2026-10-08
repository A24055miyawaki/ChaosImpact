// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/NetSerialization.h"
#include "Logging/LogMacros.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
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
class AChaosImpactTornado;
class UChaosImpactPuppetComponent;
enum class EChaosImpactBallFlightMode : uint8;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

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

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
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
	void SetGameplayUIVisible(bool bVisible);
	void CancelChargingThrow();
	void BeginThrowInput();
	void EndThrowInput();
	void RecoverStaminaFromBallHit();
	/** Called on the eliminating character so its own HUD can play the KO banner. */
	void NotifyOpponentEliminated(const FString& VictimName);
	/** Rumbles this character's controller when it is played on this machine (see AChaosImpactPlayerController::PlayRumble). */
	void PlayControllerRumble(float Small, float Big, float Seconds, float DelaySeconds = 0.0f) const;
	bool TryPickupBall(AChaosImpactBall* Ball);
	/** Carried ball type per slot; slot 0 is the right hand and is thrown next. */
	EChaosImpactBallType GetCarriedBallType(int32 Slot) const
	{
		return ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, Slot);
	}
	uint8 GetCarriedBallTypesPacked() const { return CarriedBallTypes; }
	/** Swaps the two carried balls so the left-hand ball is thrown next. Needs two balls in hand. */
	void RequestBallSwap();
	/** Drops the ball in hand (the one thrown next) on the floor in front, where anyone can pick it up (F, D-pad left). */
	void RequestDropBall();
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
	/** Selects a nova's ground target. The point is clamped and projected onto the playable stage. */
	bool SetNovaTargetPoint(const FVector& WorldPoint);
	bool HasNovaTargetPoint() const { return bNovaTargetValid; }
	FVector GetNovaTargetPoint() const { return FVector(NovaTargetLocation); }
	/** How a throw leaves: its speeds and flight, and whether it goes from over the head instead of the hand. */
	void GetThrowFlight(float ChargeAlpha, EChaosImpactBallType Type, float Scale, float& OutHorizontalSpeed,
		float& OutUpSpeed, EChaosImpactBallFlightMode& OutMode, bool& bOutOverhead) const;
	/** Where a ball held up over the head sits, from the capsule's centre. */
	FVector GetOverheadHoldOffset(EChaosImpactBallType Type, float Scale) const;
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
	/** A blast nearby shakes this player's camera (Strength 0-1, fading over Seconds). */
	void AddCameraShake(float Strength, float Seconds);
	/** Name drawn above this character: CPUs are "CPU1", "CPU2"..., players use their room or local name. */
	FString GetOverheadDisplayName() const;
	/** The character and colour this player is shown as (the results podium shows the same). */
	int32 GetShownCharacter() const;
	int32 GetShownColour() const;
	/** 1-based number shown above a CPU character; 0 for human players. */
	int32 GetCPUNumber() const { return CPUNumber; }
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
	bool IsTrainingMenuFrozen() const { return bTrainingMenuFrozen; }
	/** Moves P1 closer and to the side so the live settings panel does not cover them. */
	void SetTrainingMenuCameraActive(bool bActive);
	bool IsChargingThrow() const { return bIsChargingThrow; }
	bool IsThrowReleasePending() const { return bThrowReleasePending; }
	bool IsThrowAnimationPlaying() const { return bThrowAnimationActive; }
	bool IsPersonalAimGuideVisible() const;
	float GetAimGuideLength() const { return AimGuideLength; }
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
	/**
	 * Blown away by a tornado: moved along Velocity (horizontal), easing out over Seconds. Takes effect where this
	 * character is moved (its owner, the host, a CPU), like a black hole's pull.
	 */
	void StartWindKnockback(const FVector& Velocity, float Seconds);
	/**
	 * Caught by a tornado: drawn in and whirled round it, lifted off the ground, for WindCarrySeconds, then thrown
	 * out. Called where the catch is judged (and on the server for everyone); the position moves where this character
	 * is moved, the lift and spin show on every machine.
	 */
	void BeginWindCarry(AChaosImpactTornado* Tornado);
	bool IsCarriedByWind() const { return WindCarrier != nullptr; }
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
	/** VS team select, opening (until GO) and results: no moving, throwing, dashing or jumping. */
	bool IsMatchInputLocked() const;
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

protected:
	// Online play: inputs are predicted locally and resolved by the server.
	UFUNCTION(Server, Reliable)
	void ServerStartCharge();
	UFUNCTION(Server, Reliable)
	void ServerReleaseThrow(float ChargeAlpha, FVector_NetQuantizeNormal Aim, FVector_NetQuantize ClientLocation,
		FVector_NetQuantize ClientNovaTarget, bool bHasClientNovaTarget);
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
	UFUNCTION(Client, Reliable)
	void ClientShowRespawn(const FString& DefeatedBy, float Seconds, APawn* KillerPawn);
	UFUNCTION(Client, Reliable)
	void ClientRespawned();
	UFUNCTION(Client, Reliable)
	void ClientShowKnockout(const FString& VictimName);
	UFUNCTION(Server, Reliable)
	void ServerReportBallHit(AChaosImpactBall* Ball, FVector_NetQuantize HitLocation);
	/** Server moved this player (respawn, start point); the owner's trusted position must follow. */
	UFUNCTION(Client, Reliable)
	void ClientTeleportTo(FVector_NetQuantize Location, FRotator Rotation);
	UFUNCTION(Server, Reliable)
	void ServerClaimPickup(AChaosImpactBall* Ball);
	/** Answer to a pickup claimed from this player's screen; a refusal adopts the server's ball count. */
	UFUNCTION(Client, Reliable)
	void ClientPickupResolved(bool bAccepted, int32 ServerBallCount, uint8 ServerBallTypes, AChaosImpactBall* Ball);
	/** Answer to a throw released on this player's screen; a refusal also removes the preview. */
	UFUNCTION(Client, Reliable)
	void ClientThrowResolved(bool bAccepted, int32 ServerBallCount, uint8 ServerBallTypes);
	/** A server-side change of this player's ball count (elimination, respawn, match start). */
	UFUNCTION(Client, Reliable)
	void ClientBallCountReset(int32 ServerBallCount, uint8 ServerBallTypes);
	UFUNCTION(Server, Reliable)
	void ServerSwapBalls();
	UFUNCTION(Server, Reliable)
	void ServerDropBall();
	/** A throw charge this player cancelled on their own screen. */
	UFUNCTION(Server, Reliable)
	void ServerCancelCharge();
	/** Answer to a swap made on this player's screen; resolves it in the same ordered queue as pickups and throws. */
	UFUNCTION(Client, Reliable)
	void ClientSwapResolved(int32 ServerBallCount, uint8 ServerBallTypes);
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
	bool FindMouseAimPoint(FVector& OutAimPoint) const;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Throw", meta=(ClampMin="0.1", ClampMax="4.0"))
	float ThrowAnimationPlayRate = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory", meta=(ClampMin="1", ClampMax="2"))
	int32 MaximumCarriedBalls = 2;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing=OnRep_CarriedBallCount, BlueprintReadOnly, Category="Chaos Impact|Ball Inventory")
	int32 CarriedBallCount = 0;

	/** EChaosImpactBallType per carried slot, packed by ChaosImpactBallTypes::Pack; slot 0 is thrown first. */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing=OnRep_CarriedBallCount, Category="Chaos Impact|Ball Inventory")
	uint8 CarriedBallTypes = 0;

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
	TObjectPtr<UMaterialInstanceDynamic> HeldSimaeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RightHandBaseMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LeftHandBaseMesh;

	TWeakObjectPtr<AActor> LastDamageCauser;
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
	bool CalculateNovaLaunchToTarget(const FVector& LaunchLocation, const FVector& GroundTarget, float BallRadius,
		FVector& OutDirection, float& OutHorizontalSpeed, float& OutUpSpeed) const;
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
	FVector NovaCameraLead = FVector::ZeroVector;
	double NovaCameraHoldUntil = 0.0;
	FRotator SavedCameraBoomRotation = FRotator(-60.0f, 0.0f, 0.0f);

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
	uint8 SoundBallTypes = 0;
	float SoundHealth = -1.0f;
	/** A nova swelling up over the head hums as it grows. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> NovaChargeSound;

	void UpdateCameraShake();
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
	/** How much of a remote player's round trip their drawn body is led ahead by. */
	static constexpr float PresentationLeadShareOfPing = 0.3f;
	static constexpr float MaxPresentationLeadDistance = 260.0f;
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
	uint8 ServerAnsweredBallTypes = 0;
	/** Pickups are stored as 1 + EChaosImpactBallType, throws as -1, swaps of the two hands as -2. */
	void RefreshPredictedBallCount();
	void ResolveOldestBallAction(int32 ServerBallCount, uint8 ServerBallTypes);
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
	bool BoundActionHeld[16] = {};
	/** Reads the player's own controls (the settings screen's) and acts on them; every frame for a local player. */
	void UpdateBoundControls();
	bool bTrainingMenuFrozen = false;
	bool bTrainingMenuCameraActive = false;
	uint8 SavedTrainingMenuMovementMode = 1;
	uint8 SavedTrainingMenuCustomMovementMode = 0;
	float SavedCameraArmLength = 800.0f;
	FVector SavedCameraSocketOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Camera", meta=(ClampMin="250.0", ClampMax="800.0"))
	float TrainingMenuCameraArmLength = 490.0f;

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Camera")
	FVector TrainingMenuCameraSocketOffset = FVector(0.0f, -190.0f, 30.0f);

	UPROPERTY(EditAnywhere, Category="Chaos Impact|Training Camera", meta=(ClampMin="1.0"))
	float TrainingMenuCameraBlendSpeed = 7.5f;

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


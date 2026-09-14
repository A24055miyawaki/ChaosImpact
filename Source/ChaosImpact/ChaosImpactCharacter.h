// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/NetSerialization.h"
#include "Logging/LogMacros.h"
#include "ChaosImpactCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UInputAction;
class UChaosImpactChargeWidget;
class UAnimSequenceBase;
class UAnimInstance;
class AChaosImpactBall;
enum class EChaosImpactBallFlightMode : uint8;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

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
	bool TryPickupBall(AChaosImpactBall* Ball);
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
	void ServerReleaseThrow(float ChargeAlpha, FVector_NetQuantizeNormal Aim);
	UFUNCTION(Server, Reliable)
	void ServerStartDash(FVector_NetQuantizeNormal Direction);
	UFUNCTION(Server, Unreliable)
	void ServerUpdateAim(FVector_NetQuantizeNormal Direction);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayThrowAnimation();
	UFUNCTION(Client, Reliable)
	void ClientShowRespawn(const FString& DefeatedBy, float Seconds, APawn* KillerPawn);
	UFUNCTION(Client, Reliable)
	void ClientRespawned();
	UFUNCTION(Client, Reliable)
	void ClientShowKnockout(const FString& VictimName);
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
	virtual void Tick(float DeltaSeconds) override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for right-stick aiming input. Mouse aiming is read from the cursor. */
	void AimWithStick(const FInputActionValue& Value);
	void StopAimingWithStick(const FInputActionValue& Value);

	/** Starts and releases a charged throw. */
	void StartChargingThrow();
	void ReleaseChargedThrow();

	/** Updates aim from the mouse cursor or right stick. */
	void UpdateAim(float DeltaSeconds);
	FVector ApplyControllerAimAssist(const FVector& RawDirection) const;
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

	/** Gentle target attraction keeps stick aiming quick without turning it into auto-aim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="45.0"))
	float ControllerAimAssistAngleDegrees = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0"))
	float ControllerAimAssistDistance = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ControllerAimAssistStrength = 0.38f;

	/** Length of the temporary aiming arrow drawn while charging. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Aim", meta=(ClampMin="0.0"))
	float AimGuideLength = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Damage", meta=(ClampMin="1.0"))
	float MaxHealth = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Damage", meta=(ClampMin="0.0"))
	float EliminationResetDelay = 3.0f;

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

	UPROPERTY(VisibleInstanceOnly, Replicated, BlueprintReadOnly, Category="Chaos Impact|Dash")
	float Stamina = 5.0f;

	/** Other machines only draw the dash trail; the dash itself runs on the owner and server. */
	UPROPERTY(ReplicatedUsing=OnRep_ReplicatedDashing)
	bool bReplicatedDashing = false;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal ReplicatedDashDirection = FVector::ForwardVector;

	UPROPERTY(ReplicatedUsing=OnRep_Eliminated)
	bool bEliminated = false;

	double NextAimSendAt = 0.0;
	FVector LastSentAim = FVector::ZeroVector;

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
	TWeakObjectPtr<AChaosImpactBall> PendingThrowBall;
	TSubclassOf<UAnimInstance> LocomotionAnimInstanceClass;
	FVector PendingThrowDirection = FVector::ForwardVector;
	float PendingThrowSpeed = 0.0f;
	float PendingThrowArcUpwardSpeed = 0.0f;
	EChaosImpactBallFlightMode PendingThrowFlightMode;
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
	float DashDistanceApplied = 0.0f;
	float NextDashAvailableAtSeconds = 0.0f;
	bool bIsChargingThrow = false;
	bool bThrowReleasePending = false;
	bool bThrowAnimationActive = false;
	bool bIsDashing = false;
	bool bWasFallingBeforeDash = false;
	bool bMouseChargeActive = false;
	bool bWasMouseDownLastTick = false;
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


#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChaosImpactShoulderCameraComponent.generated.h"

class AChaosImpactCharacter;
class AChaosImpactPlayerController;

/** Removable, local-only training experiment. Never changes projectile physics or saved control profiles. */
UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class UChaosImpactShoulderCameraComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UChaosImpactShoulderCameraComponent();
	/** Called before the character's camera/movement/aim update (not a separate component tick). */
	void UpdateCamera(float DeltaSeconds);
	bool IsShoulderViewActive() const { return bActive; }
	FRotator GetViewRotation() const { return FRotator(Pitch, Yaw, 0); }
	FVector GetFlatAimDirection() const { return FRotator(0, Yaw, 0).Vector(); }
	/**
	 * Where a throw from From goes: flat, toward the point under the crosshair (an enemy there, else what the view ray
	 * meets, never nearer than a short throw), so the ball flies where the crosshair is though the camera sits beside
	 * the character. Within MaxConvergeDegrees of the camera's own yaw.
	 */
	FVector GetThrowAimDirection(const FVector& From) const;
	/** The point the crosshair is on (world), and the enemy or target under it (null for none). */
	FVector GetAimPoint() const { return AimPoint; }
	const AActor* GetAimTarget() const { return AimTarget.Get(); }
	/** Engine mouse Y is positive upwards; engine pad Y is positive downwards. */
	static FVector2D MouseLookDegrees(FVector2D Delta, float Sensitivity);
	static FVector2D PadLookDegrees(FVector2D Raw, float DeltaSeconds, float DeadZone, float YawRate, float PitchRate);
	/** Direction marker: render-camera yaw, fixed depth and throw height, never an obstacle-dependent hit point. */
	static FVector MakeReticleAnchor(FVector ViewLocation, FRotator ViewRotation, float ThrowHeight);
	/** The point a flat throw aims at along the view's yaw: at Depth, but never nearer the character than MinThrow. */
	static FVector MakeConvergencePoint(FVector ViewLocation, float ViewYaw, float Depth, FVector Character, float MinThrow);
	void AddLookDegrees(float YawDegrees, float PitchDegrees);
	/** Current saved profile/device scale is read on each input, without resetting view or held buttons. */
	FVector2D GetLookDegreesForPlayer(const AChaosImpactPlayerController* PC, FVector2D Input, float DeltaSeconds) const;

	/** The boom while aiming (charging a throw: in a little closer) and the shoulder it sits over (+1 right, -1 left). */
	float GetDesiredArmLength(bool bCharging) const { return bCharging ? ChargeArmLength : ArmLength; }
	FVector GetDesiredSocketOffset(bool bCharging) const;
	float GetShoulderSide() const { return Side; }
	/** Just switched on: the character eases the view round from top-down instead of cutting to it. */
	bool IsBlendingIn() const { return bBlendingIn; }
	void FinishBlendIn() { bBlendingIn = false; }

	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="150", ClampMax="900"))
	float ArmLength = 430;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="150", ClampMax="900"))
	float ChargeArmLength = 330;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera")
	FVector SocketOffset = FVector(0, 72, 18);
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera")
	FVector ChargeSocketOffset = FVector(0, 66, 26);
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera")
	FVector TargetOffset = FVector(0, 0, 64);
	/** Degrees narrower while charging (a little zoom for aiming). */
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="30"))
	float ChargeFovDrop = 10;
	/** Look speed while charging (finer aim). */
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0.2", ClampMax="1"))
	float ChargeLookScale = 0.7f;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0.01", ClampMax="2"))
	float MouseSensitivity = 0.28f;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="10", ClampMax="720"))
	float PadYawRate = 320;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="10", ClampMax="360"))
	float PadPitchRate = 200;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="0.9"))
	float PadDeadZone = 0.20f;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="-45", ClampMax="0"))
	float MinimumPitch = -32;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="40"))
	float MaximumPitch = 18;
	/** Pad with エイムアシスト: look slows over an enemy and gently follows one near the crosshair. */
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="15"))
	float AssistAngle = 6;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0.1", ClampMax="1"))
	float AssistFriction = 0.5f;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="180"))
	float AssistFollowDegreesPerSecond = 45;
	/** The shortest throw the crosshair converges at, and how far a throw may turn from the camera's yaw. */
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="100", ClampMax="2000"))
	float MinConvergeDistance = 600;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="30"))
	float MaxConvergeDegrees = 14;

protected:
	virtual void BeginPlay() override;
private:
	/** What is under the crosshair (one line test and a look over the few characters / targets). */
	void UpdateAimPoint(const AChaosImpactCharacter* Character, const FVector& ViewLocation);
	/** Over the left shoulder while a wall hugs the right one (and back once it is clear for a moment). */
	void UpdateShoulderSide(const AChaosImpactCharacter* Character, double Now);
	/** The character fades out of its own view when a wall pushes the camera right up to it. */
	void UpdateOwnerVisibility(AChaosImpactCharacter* Character, const FVector& ViewLocation, bool bShow);
	/** Enemy near the crosshair (pad aim assist): its yaw from the camera, and how far off the crosshair. */
	bool FindAssistTarget(const AChaosImpactCharacter* Character, const FVector& ViewLocation, float& OutYaw, float& OutOffDegrees) const;

	bool bActive = false;
	bool bRequestedLastFrame = false;
	bool bOriginalCollisionTest = false;
	bool bSkipFirstInput = true;
	bool bBlendingIn = false;
	bool bOwnerHidden = false;
	float Yaw = 0;
	float Pitch = -12;
	float Side = 1;
	double BlendStartedAt = 0;
	double SideBlockedSince = -1;
	double SideClearSince = -1;
	FVector AimPoint = FVector::ZeroVector;
	bool bAimPointValid = false;
	TWeakObjectPtr<const AActor> AimTarget;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChaosImpactShoulderCameraComponent.generated.h"

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
	/** Engine mouse Y is positive upwards; engine pad Y is positive downwards. */
	static FVector2D MouseLookDegrees(FVector2D Delta, float Sensitivity);
	static FVector2D PadLookDegrees(FVector2D Raw, float DeltaSeconds, float DeadZone, float YawRate, float PitchRate);
	/** Direction marker: render-camera yaw, fixed depth and throw height, never an obstacle-dependent hit point. */
	static FVector MakeReticleAnchor(FVector ViewLocation, FRotator ViewRotation, float ThrowHeight);
	void AddLookDegrees(float YawDegrees, float PitchDegrees);
	/** Current saved profile/device scale is read on each input, without resetting view or held buttons. */
	FVector2D GetLookDegreesForPlayer(const AChaosImpactPlayerController* PC, FVector2D Input, float DeltaSeconds) const;

	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="150", ClampMax="900"))
	float ArmLength = 460;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera")
	FVector SocketOffset = FVector(0, 70, 20);
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera")
	FVector TargetOffset = FVector(0, 0, 64);
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0.01", ClampMax="2"))
	float MouseSensitivity = 0.28f;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="10", ClampMax="720"))
	float PadYawRate = 320;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="10", ClampMax="360"))
	float PadPitchRate = 200;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="0.9"))
	float PadDeadZone = 0.20f;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="-45", ClampMax="0"))
	float MinimumPitch = -25;
	UPROPERTY(EditAnywhere, Category="Training Shoulder Camera", meta=(ClampMin="0", ClampMax="40"))
	float MaximumPitch = 15;

protected:
	virtual void BeginPlay() override;
private:
	bool bActive = false;
	bool bRequestedLastFrame = false;
	bool bOriginalCollisionTest = false;
	bool bSkipFirstInput = true;
	float Yaw = 0;
	float Pitch = -12;
};

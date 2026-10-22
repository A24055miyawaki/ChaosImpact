#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChaosImpactBallFormComponent.generated.h"

class AChaosImpactBuddyBall;
class AChaosImpactCharacter;

/**
 * Server-authoritative teammate ball, in training and local/online VS team battles.
 * The original pawn keeps its controller and camera.
 */
UCLASS(ClassGroup=(ChaosImpact))
class UChaosImpactBallFormComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UChaosImpactBallFormComponent();
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Ball Form")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Ball Form", meta=(ClampMin="0.8"))
	float HoldSeconds = 1.3f;
	bool IsAvailable() const;
	bool IsInBallForm() const { return FormBall.IsValid(); }
	AChaosImpactBuddyBall* GetFormBall() const { return FormBall.Get(); }
	AChaosImpactBuddyBall* GetHeldBall(int32 Slot) const;
	bool CanCollect(const AChaosImpactCharacter* Passenger) const;
	bool Toggle();
	void Restore(bool bRelocate = true);
	/** Returns true while the body must not run ordinary character input. */
	bool HandleInput();
	void ResetGesture();
	/** Owner input only. Remote steering is rate-limited and validated by the server. */
	void Steer(const FVector& Direction);
	void Push(AChaosImpactBuddyBall* Ball, int32 Slot);
	void Pop();
	void SwapSlots();
	void Clear();
	void Remove(AChaosImpactBuddyBall* Ball);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UFUNCTION(Server, Reliable)
	void ServerToggle();
	UFUNCTION(Server, Unreliable)
	void ServerSteer(FVector_NetQuantizeNormal Direction);
	UFUNCTION()
	void OnRep_FormBall();
	UFUNCTION()
	void OnRep_Slots();
	void ApplyFormPresentation();
	AChaosImpactCharacter* Character() const;
	UPROPERTY(ReplicatedUsing=OnRep_FormBall)
	TWeakObjectPtr<AChaosImpactBuddyBall> FormBall;
	UPROPERTY(ReplicatedUsing=OnRep_Slots)
	TWeakObjectPtr<AChaosImpactBuddyBall> Slots[2];
	double NextSteerAt = 0.0;
	FVector SafeLocation = FVector::ZeroVector;
	FTransform SavedPuppetTransform;
	bool bSavedPuppet = false;
	uint8 SavedCollision = 0;
	uint8 SavedMovement = 1;
	uint8 SavedCustomMovement = 0;
	bool bRestoring = false;
	bool bHeld = false;
	bool bTriggered = false;
	bool bWaitForManageRelease = false;
	double PressedAt = 0.0;
	double NextTransformAt = 0.0;
	/** Seconds since curling up (the body shrinking into the shell), and since uncurling (springing back), or < 0. */
	float CurlAge = -1.0f;
	float UncurlAge = -1.0f;
	/** The curled body's middle, in the body's own space (measured once it has curled), kept at the shell's centre. */
	FVector CurlCenter = FVector(0.0f, 0.0f, 85.0f);
	bool bCurlCenterKnown = false;
};

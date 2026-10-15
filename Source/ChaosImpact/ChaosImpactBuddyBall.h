#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBuddyBall.generated.h"

class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMeshComponent;

/**
 * A curled-up player riding an otherwise ordinary ball: same charge, damage and wall rebounds.
 *
 * Its look: a glassy shell in the player's team colour (seams like a dodgeball's, the rim glowing) with the curled
 * player inside. It pops out of the player when they curl up; waiting to be picked up it bobs, a ring of the team's
 * colour pulsing on the floor under it; held, it sits smaller in the hand; thrown, it rolls through the air leaving a
 * trail of its colour; and it bursts when the player uncurls (PlayPop).
 */
UCLASS()
class AChaosImpactBuddyBall : public AChaosImpactBall
{
	GENERATED_BODY()
public:
	AChaosImpactBuddyBall();
	void InitializePassenger(AChaosImpactCharacter* Character);
	AChaosImpactCharacter* GetPassenger() const { return Passenger.Get(); }
	AChaosImpactCharacter* GetCarrier() const { return Carrier.Get(); }
	bool IsLaunched() const { return bLaunched; }
	void SetSteering(const FVector& Input) { Steering = Input.GetClampedToMaxSize(1.0f); }
	virtual bool ConsumePickup(AChaosImpactCharacter* Collector) override;
	virtual bool IsPickupAvailable() const override;
	virtual void Launch(const FVector& Direction, float Speed, EChaosImpactBallFlightMode FlightMode, float ArcUpwardSpeed) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer) override;
	void ReleasePassenger();
	void ForgetPassenger() { Passenger.Reset(); }
	void FollowHand();

	/** The shell's roll (the curled player inside turns with it). */
	FQuat GetRoll() const { return Roll; }
	/** Where the shell's centre is drawn (it bobs while waiting). */
	FVector GetShellCenter() const;
	/** How far the shell has popped out (0 - 1, overshooting a little on the way). */
	float GetAppear() const;
	/** The passenger's team colour (or a pale blue without a team). */
	FLinearColor GetTeamColor() const;
	/** The shell bursting and sparks of the colour: when a player curls up or uncurls. */
	static void PlayPop(const UObject* WorldContext, const FVector& At, const FLinearColor& Color, bool bUncurl);
	/** The shell's radius (the ball's collision is smaller, so it rolls like a ball). */
	static constexpr float ShellRadius = 46.0f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void UpdateLook(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Buddy Ball")
	TObjectPtr<UStaticMeshComponent> Shell;
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Buddy Ball")
	TObjectPtr<UStaticMeshComponent> FloorRing;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShellLook;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingLook;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Trail;
	/** Afterimages of the shell behind it in flight, in the team's colour (nearest first). */
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Buddy Ball")
	TArray<TObjectPtr<UStaticMeshComponent>> Ghosts;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostLooks;
	/** Where the shell has been lately (newest first), for the afterimages. */
	TArray<FVector> Trace;
	float TraceClock = 0.0f;

	TWeakObjectPtr<AChaosImpactCharacter> Passenger;
	TWeakObjectPtr<AChaosImpactCharacter> Carrier;
	FVector Steering = FVector::ZeroVector;
	bool bLaunched = false;
	double LaunchedAt = 0.0;
	float Age = 0.0f;
	float Bob = 0.0f;
	/** A flash when thrown (1, fading). */
	float Flash = 0.0f;
	FQuat Roll = FQuat::Identity;
};

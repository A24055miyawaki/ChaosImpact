#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBuddyBall.generated.h"

/** A curled-up player riding an otherwise ordinary ball: same charge, damage and wall rebounds. */
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
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	TWeakObjectPtr<AChaosImpactCharacter> Passenger;
	TWeakObjectPtr<AChaosImpactCharacter> Carrier;
	FVector Steering = FVector::ZeroVector;
	bool bLaunched = false;
	double LaunchedAt = 0.0;
};

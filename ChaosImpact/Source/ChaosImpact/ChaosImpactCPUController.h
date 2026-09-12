#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ChaosImpactCPUController.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;

/** Lightweight training opponent: collects balls, aims, charges, throws and dodges. */
UCLASS()
class AChaosImpactCPUController : public AAIController
{
	GENERATED_BODY()

public:
	AChaosImpactCPUController();
	virtual void Tick(float DeltaSeconds) override;
	bool UsesArcFlightMode() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	AChaosImpactBall* FindNearestPickup(const FVector& From) const;
	AChaosImpactCharacter* FindNearestOpponent(const FVector& From) const;
	bool TryDodgeIncomingBall(AChaosImpactCharacter* ControlledCharacter, float Now);
	void RefreshDecision(AChaosImpactCharacter* ControlledCharacter, float Now);
	FVector AvoidNearbyObstacle(const FVector& Origin, const FVector& DesiredDirection) const;
	bool HasLowObstacleAhead(const FVector& Origin, const FVector& DesiredDirection) const;

	float NextDecisionAt = 0.0f;
	float NextThrowAt = 0.0f;
	float ReleaseThrowAt = 0.0f;
	float NextDashAt = 0.0f;
	float NextStrafeChangeAt = 0.0f;
	float NextJumpAt = 0.0f;
	float StopJumpAt = 0.0f;
	FVector DesiredMoveDirection = FVector::ZeroVector;
	float DesiredMoveScale = 0.0f;
	float StrafeSign = 1.0f;
	bool bChargingThrow = false;
	bool bHoldingJump = false;
};

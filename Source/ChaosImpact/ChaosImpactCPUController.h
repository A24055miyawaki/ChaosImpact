#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ChaosImpactCPUController.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;

/**
 * CPU opponent that plays the same character under the same rules as a human.
 * It simulates ball flight (arc gravity, floor pickup, wall reflections and the release delay),
 * dodges by testing walk/dash/jump escapes against every incoming ball, leads moving targets,
 * searches bank shots, times throws against committed opponents and manages balls and stamina.
 */
UCLASS()
class AChaosImpactCPUController : public AAIController
{
	GENERATED_BODY()

public:
	AChaosImpactCPUController();
	virtual void Tick(float DeltaSeconds) override;
	bool UsesArcFlightMode() const;

	/** 0 = relaxed sparring partner, 1 = full strength. Scales reaction time, aim error and tactics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|CPU", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Skill = 1.0f;

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	struct FShotPlan
	{
		bool bValid = false;
		bool bBankShot = false;
		FVector Direction = FVector::ForwardVector;
		/** Seconds from the throw input until the ball reaches the target, including release delay. */
		float ArrivalSeconds = 0.0f;
		float Quality = 0.0f;
	};

	struct FBallThreat
	{
		TArray<FVector> Path;
		float StepSeconds = 0.0f;
	};

	// Perception
	void UpdatePerception(const AChaosImpactCharacter* Self, float DeltaSeconds, double Now);
	FVector GetObservedVelocity(const AChaosImpactCharacter* Observed) const;
	float GetReactionSeconds() const;

	// Ball flight prediction
	void SimulateBallPath(const FVector& Start, FVector Velocity, bool bArc, float HorizonSeconds,
		float StepSeconds, const AActor* IgnoredA, const AActor* IgnoredB, TArray<FVector>& OutPath) const;
	float GetArcFlightLimitSeconds() const;

	// Defence
	void CollectThreats(const AChaosImpactCharacter* Self, double Now, TArray<FBallThreat>& OutThreats) const;
	float EvaluateEscape(const AChaosImpactCharacter* Self, const TArray<FBallThreat>& Threats,
		const FVector& Direction, float TravelLimit, bool bDash, bool bJump) const;
	float EarliestContactSeconds(const AChaosImpactCharacter* Self, const TArray<FBallThreat>& Threats) const;
	bool UpdateEvasion(AChaosImpactCharacter* Self, float Now);

	// Offence
	AChaosImpactCharacter* SelectTarget(const AChaosImpactCharacter* Self) const;
	FShotPlan PlanShot(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target,
		float ChargeAlpha, bool bAllowBank) const;
	float FindShotArrival(const TArray<FVector>& Path, float StepSeconds, const AChaosImpactCharacter* Target,
		const FVector& TargetVelocity, float LaunchDelay) const;
	float ChooseDesiredCharge(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target) const;
	void UpdateOffense(AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, float Now);

	// Positioning and ball economy
	AChaosImpactBall* SelectPickup(const AChaosImpactCharacter* Self) const;
	bool TryCollectNearbyBall(AChaosImpactCharacter* Self, float Now);
	void UpdatePositioning(AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, float Now);

	// Navigation
	FVector AvoidNearbyObstacle(const FVector& Origin, const FVector& DesiredDirection) const;
	/** Steering with a short commitment, so the CPU cannot dither between two ways around the same wall. */
	FVector SteerAroundObstacles(const FVector& Origin, const FVector& DesiredDirection, float Now);
	/** Walls are hit; balls and other characters are not. Returns how far it got and the wall it met. */
	float SweepWalls(const FVector& Origin, const FVector& Direction, float MaxDistance,
		FVector& OutWallNormal, const AActor* IgnoredActor = nullptr) const;
	/** Keeps characters from piling into each other while they share a lane. */
	FVector GetSeparationDirection(const AChaosImpactCharacter* Self) const;
	bool IsPathClear(const FVector& Origin, const FVector& Direction, float Distance) const;
	float GetFreeTravel(const FVector& Origin, const FVector& Direction, float MaxDistance,
		const AActor* IgnoredActor = nullptr) const;
	bool HasLowObstacleAhead(const FVector& Origin, const FVector& DesiredDirection) const;
	void TryTraverseObstacle(AChaosImpactCharacter* ControlledCharacter,
		const FVector& Direction, float Now);
	void UpdateStuckRecovery(AChaosImpactCharacter* ControlledCharacter, float Now);

	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> LastObservedLocations;
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> ObservedVelocities;
	/** How the target's speed is changing, so a shot leads a player who is still turning. */
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> ObservedAccelerations;
	FVector GetObservedAcceleration(const AChaosImpactCharacter* Observed) const;
	TMap<TWeakObjectPtr<AChaosImpactBall>, double> BallFirstSeenAt;
	TWeakObjectPtr<AChaosImpactCharacter> CurrentTarget;

	/** Height of the ball above this character's feet at release; refined from observed throws. */
	float LearnedReleaseHeight = 130.0f;
	float DesiredChargeAlpha = 0.5f;
	float ChargeStartedAt = 0.0f;
	float LastValidShotAt = 0.0f;
	float ShotAimErrorDegrees = 0.0f;

	float NextDecisionAt = 0.0f;
	float NextThrowAt = 0.0f;
	float NextBankSearchAt = 0.0f;
	float NextStrafeChangeAt = 0.0f;
	float NextJumpAt = 0.0f;
	float StopJumpAt = 0.0f;
	FVector DesiredMoveDirection = FVector::ZeroVector;
	FVector EscapeMoveDirection = FVector::ZeroVector;
	FVector EvadeDirection = FVector::ZeroVector;
	FVector LastProgressLocation = FVector::ZeroVector;
	/** The way around an obstacle that is being followed, and how long it is kept before rethinking. */
	FVector AvoidCommitDirection = FVector::ZeroVector;
	float AvoidCommitUntil = 0.0f;
	/** Consecutive checks that made no progress; each one tries a stronger way out. */
	int32 StuckStreak = 0;
	FVector HomeLocation = FVector::ZeroVector;
	float DesiredMoveScale = 0.0f;
	float LastProgressCheckAt = 0.0f;
	float EscapeUntil = 0.0f;
	float EvadeUntil = 0.0f;
	float StrafeSign = 1.0f;
	bool bHoldingJump = false;
};

#include "ChaosImpactCPUController.h"

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** Matches AChaosImpactBall's collision sphere. */
	constexpr float BallRadius = 24.0f;
	constexpr float ThreatStepSeconds = 0.025f;
	constexpr float ThreatHorizonSeconds = 1.1f;
	constexpr float ShotStepSeconds = 0.02f;
	/** Extra distance a dodge must keep from a ball beyond touching. */
	constexpr float EvasionSafeMargin = 26.0f;
	/** Human reaction time assumed when judging how hard a shot is to dodge. */
	constexpr float OpponentReactionSeconds = 0.24f;
	constexpr float PickupReach = 108.0f;
}

AChaosImpactCPUController::AChaosImpactCPUController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
}

void AChaosImpactCPUController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	NextDecisionAt = Now + 0.2f;
	NextThrowAt = Now + 0.9f;
	NextBankSearchAt = Now;
	NextStrafeChangeAt = Now + 1.0f;
	NextJumpAt = Now + 0.5f;
	StrafeSign = FMath::FRand() < 0.5f ? -1.0f : 1.0f;
	HomeLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	LastProgressLocation = HomeLocation;
	LastProgressCheckAt = Now;
	EscapeUntil = 0.0f;
	EvadeUntil = 0.0f;
	LastObservedLocations.Reset();
	ObservedVelocities.Reset();
	BallFirstSeenAt.Reset();
	CurrentTarget.Reset();
}

void AChaosImpactCPUController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AChaosImpactCharacter* Self = Cast<AChaosImpactCharacter>(GetPawn());
	if (!Self || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	UpdatePerception(Self, DeltaSeconds, Now);
	if (Self->IsEliminated())
	{
		EvadeUntil = 0.0f;
		return;
	}
	if (Self->IsTrainingMenuFrozen())
	{
		// World time and physics keep running, but the CPU itself must stand still.
		if (Self->IsChargingThrow())
		{
			Self->CancelChargingThrow();
			NextThrowAt = Now + 0.6f;
		}
		if (bHoldingJump)
		{
			Self->StopJumping();
			bHoldingJump = false;
		}
		DesiredMoveDirection = FVector::ZeroVector;
		DesiredMoveScale = 0.0f;
		EvadeUntil = 0.0f;
		return;
	}
	if (bHoldingJump && Now >= StopJumpAt)
	{
		Self->StopJumping();
		bHoldingJump = false;
	}

	if (Now >= NextDecisionAt)
	{
		NextDecisionAt = Now + FMath::Lerp(0.12f, 0.045f, Skill);
		AChaosImpactCharacter* Target = SelectTarget(Self);
		CurrentTarget = Target;
		TryCollectNearbyBall(Self, Now);
		const bool bEvading = UpdateEvasion(Self, Now);
		UpdateOffense(Self, Target, Now);
		if (!bEvading)
		{
			UpdatePositioning(Self, Target, Now);
		}
	}
	UpdateStuckRecovery(Self, Now);

	// Decisions are throttled, but movement input must be supplied every frame.
	// Otherwise CharacterMovement consumes it and the CPU visibly stutters.
	const bool bEvading = Now < EvadeUntil && !EvadeDirection.IsNearlyZero();
	const FVector ActiveMoveDirection = bEvading ? EvadeDirection
		: Now < EscapeUntil ? EscapeMoveDirection : DesiredMoveDirection;
	if (!Self->IsDashing() && !ActiveMoveDirection.IsNearlyZero())
	{
		Self->AddMovementInput(ActiveMoveDirection, bEvading ? 1.0f : DesiredMoveScale);
	}
}

// ---------------------------------------------------------------------------------------------
// Perception

void AChaosImpactCPUController::UpdatePerception(
	const AChaosImpactCharacter* Self, const float DeltaSeconds, const double Now)
{
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.001f);
	const float Blend = 1.0f - FMath::Exp(-SafeDelta * 18.0f);
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Observed = *It;
		const FVector Location = Observed->GetActorLocation();
		FVector Measured = Observed->GetVelocity();
		if (const FVector* LastLocation = LastObservedLocations.Find(Observed))
		{
			const FVector Displacement = (Location - *LastLocation) / SafeDelta;
			if (Displacement.Size2D() > 4000.0f)
			{
				// Respawn teleport, not motion.
				Measured = FVector::ZeroVector;
			}
			else if (Observed->IsDashing())
			{
				// Dashes move the actor directly, so the movement component reports zero velocity.
				Measured = Displacement;
			}
		}
		FVector& Smoothed = ObservedVelocities.FindOrAdd(Observed);
		Smoothed = FMath::Lerp(Smoothed, Measured, Blend);
		LastObservedLocations.Add(Observed, Location);
	}

	for (auto It = BallFirstSeenAt.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Key()->IsPickup())
		{
			It.RemoveCurrent();
		}
	}
	const float SelfFeet = Self->GetActorLocation().Z
		- Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		if (Ball->IsPickup() || Ball->GetBallVelocity().SizeSquared() < FMath::Square(200.0f)
			|| BallFirstSeenAt.Contains(Ball))
		{
			continue;
		}
		BallFirstSeenAt.Add(Ball, Now);
		if (Ball->WasThrownBy(Self))
		{
			// The release point comes from the throw animation, so learn it from our own balls.
			const float Height = Ball->GetActorLocation().Z - SelfFeet;
			if (Height > 40.0f && Height < 260.0f)
			{
				LearnedReleaseHeight = FMath::Lerp(LearnedReleaseHeight, Height, 0.6f);
			}
		}
	}
}

FVector AChaosImpactCPUController::GetObservedVelocity(const AChaosImpactCharacter* Observed) const
{
	if (const FVector* Velocity = ObservedVelocities.Find(Observed))
	{
		return *Velocity;
	}
	return Observed ? Observed->GetVelocity() : FVector::ZeroVector;
}

float AChaosImpactCPUController::GetReactionSeconds() const
{
	return FMath::Lerp(0.34f, 0.07f, Skill);
}

// ---------------------------------------------------------------------------------------------
// Ball flight prediction

void AChaosImpactCPUController::SimulateBallPath(const FVector& Start, FVector Velocity, const bool bArc,
	const float HorizonSeconds, const float StepSeconds, const AActor* IgnoredA, const AActor* IgnoredB,
	TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	OutPath.Add(Start);
	if (!GetWorld())
	{
		return;
	}
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUBallPath), false, GetPawn());
	Parameters.AddIgnoredActor(IgnoredA);
	Parameters.AddIgnoredActor(IgnoredB);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(BallRadius);
	const float GravityZ = GetWorld()->GetGravityZ();
	FVector Position = Start;
	const int32 Steps = FMath::CeilToInt(HorizonSeconds / StepSeconds);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		FVector NextVelocity = Velocity;
		if (bArc)
		{
			NextVelocity.Z += GravityZ * StepSeconds;
		}
		const FVector Next = Position + (Velocity + NextVelocity) * 0.5f * StepSeconds;
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, Position, Next, FQuat::Identity,
			ECC_Visibility, Sphere, Parameters) && !Hit.bStartPenetrating)
		{
			// Arc balls turn into harmless pickups on their first floor contact.
			if (bArc && Hit.ImpactNormal.Z > 0.65f)
			{
				OutPath.Add(Hit.Location);
				return;
			}
			FVector Normal = Hit.ImpactNormal;
			if (!bArc)
			{
				Normal.Z = 0.0f;
			}
			Normal = Normal.GetSafeNormal();
			Position = Hit.Location;
			NextVelocity = (NextVelocity - 2.0f * FVector::DotProduct(NextVelocity, Normal) * Normal)
				* (bArc ? 0.72f : 1.0f);
			if (!bArc)
			{
				NextVelocity.Z = 0.0f;
			}
		}
		else
		{
			Position = Next;
		}
		Velocity = NextVelocity;
		OutPath.Add(Position);
	}
}

float AChaosImpactCPUController::GetArcFlightLimitSeconds() const
{
	const float Gravity = GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) : 980.0f;
	return FMath::Sqrt(FMath::Max(10.0f, LearnedReleaseHeight - BallRadius) / (0.5f * FMath::Max(Gravity, 1.0f)));
}

// ---------------------------------------------------------------------------------------------
// Defence

void AChaosImpactCPUController::CollectThreats(
	const AChaosImpactCharacter* Self, const double Now, TArray<FBallThreat>& OutThreats) const
{
	const bool bArc = UsesArcFlightMode();
	const float Reaction = GetReactionSeconds();
	const FVector Location = Self->GetActorLocation();
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		if (Ball->IsPickup() || Ball->WasThrownBy(Self))
		{
			continue;
		}
		const FVector Velocity = Ball->GetBallVelocity();
		if (Velocity.SizeSquared() < FMath::Square(200.0f))
		{
			continue;
		}
		const double* SeenAt = BallFirstSeenAt.Find(Ball);
		if (!SeenAt || Now - *SeenAt < Reaction)
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Location, Ball->GetActorLocation());
		if (Distance > Velocity.Size2D() * ThreatHorizonSeconds + 300.0f)
		{
			continue;
		}

		FBallThreat Threat;
		Threat.StepSeconds = ThreatStepSeconds;
		SimulateBallPath(Ball->GetActorLocation(), Velocity, bArc, ThreatHorizonSeconds,
			ThreatStepSeconds, Ball, nullptr, Threat.Path);
		float Closest = TNumericLimits<float>::Max();
		for (const FVector& Point : Threat.Path)
		{
			Closest = FMath::Min(Closest, FVector::Dist2D(Point, Location));
		}
		// A stationary character can be reached by any path passing within one dodge radius.
		if (Closest < 420.0f)
		{
			OutThreats.Add(MoveTemp(Threat));
		}
	}
}

float AChaosImpactCPUController::EvaluateEscape(const AChaosImpactCharacter* Self,
	const TArray<FBallThreat>& Threats, const FVector& Direction, const float TravelLimit,
	const bool bDash, const bool bJump) const
{
	const UCapsuleComponent* Capsule = Self->GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = Self->GetCharacterMovement();
	const FVector Start = Self->GetActorLocation();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float HitRadius = Capsule->GetScaledCapsuleRadius() + BallRadius;
	const float WalkSpeed = Movement->MaxWalkSpeed;
	const float DashSeconds = FMath::Max(Self->GetDashDuration(), 0.01f);
	const float GravityZ = GetWorld()->GetGravityZ() * Movement->GravityScale;
	float Worst = TNumericLimits<float>::Max();
	for (const FBallThreat& Threat : Threats)
	{
		for (int32 Index = 0; Index < Threat.Path.Num(); ++Index)
		{
			const float T = Index * Threat.StepSeconds;
			float Travel = 0.0f;
			float Lift = 0.0f;
			if (bDash)
			{
				if (T < DashSeconds)
				{
					// Dashing characters take no damage.
					continue;
				}
				Travel = Self->GetDashDistance() + WalkSpeed * (T - DashSeconds);
			}
			else if (bJump)
			{
				Lift = FMath::Max(0.0f, Movement->JumpZVelocity * T + 0.5f * GravityZ * T * T);
				Travel = WalkSpeed * T * 0.5f;
			}
			else
			{
				Travel = WalkSpeed * T;
			}
			const FVector Body = Start + Direction * FMath::Min(Travel, TravelLimit) + FVector::UpVector * Lift;
			const FVector& Ball = Threat.Path[Index];
			if (FMath::Abs(Ball.Z - Body.Z) > HalfHeight + BallRadius)
			{
				continue;
			}
			Worst = FMath::Min(Worst, FVector::Dist2D(Ball, Body) - HitRadius);
		}
	}
	return Worst;
}

float AChaosImpactCPUController::EarliestContactSeconds(
	const AChaosImpactCharacter* Self, const TArray<FBallThreat>& Threats) const
{
	const UCapsuleComponent* Capsule = Self->GetCapsuleComponent();
	const FVector Location = Self->GetActorLocation();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float DangerRadius = Capsule->GetScaledCapsuleRadius() + BallRadius + EvasionSafeMargin;
	float Earliest = TNumericLimits<float>::Max();
	for (const FBallThreat& Threat : Threats)
	{
		for (int32 Index = 0; Index < Threat.Path.Num(); ++Index)
		{
			const FVector& Ball = Threat.Path[Index];
			if (FMath::Abs(Ball.Z - Location.Z) <= HalfHeight + BallRadius
				&& FVector::Dist2D(Ball, Location) < DangerRadius)
			{
				Earliest = FMath::Min(Earliest, Index * Threat.StepSeconds);
				break;
			}
		}
	}
	return Earliest;
}

bool AChaosImpactCPUController::UpdateEvasion(AChaosImpactCharacter* Self, const float Now)
{
	if (Self->IsDashing())
	{
		return Now < EvadeUntil;
	}
	TArray<FBallThreat> Threats;
	CollectThreats(Self, Now, Threats);
	if (Threats.IsEmpty()
		|| EvaluateEscape(Self, Threats, FVector::ZeroVector, 0.0f, false, false) >= EvasionSafeMargin)
	{
		return Now < EvadeUntil;
	}

	const FVector Location = Self->GetActorLocation();
	// Keep a committed dodge while it still works, so the CPU does not dither between sides.
	if (Now < EvadeUntil && !EvadeDirection.IsNearlyZero()
		&& EvaluateEscape(Self, Threats, EvadeDirection,
			GetFreeTravel(Location, EvadeDirection, 700.0f), false, false) >= EvasionSafeMargin)
	{
		return true;
	}

	const float Contact = EarliestContactSeconds(Self, Threats);
	FVector BestWalk = FVector::ZeroVector;
	float BestWalkClearance = -TNumericLimits<float>::Max();
	float BestWalkScore = -TNumericLimits<float>::Max();
	for (int32 Candidate = 0; Candidate < 16; ++Candidate)
	{
		const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 22.5f, FVector::UpVector);
		const float Free = GetFreeTravel(Location, Direction, 700.0f);
		const float Clearance = EvaluateEscape(Self, Threats, Direction, Free, false, false);
		// Among safe escapes, prefer ones that keep the current plan and head into open space.
		const float Score = FMath::Min(Clearance, 90.0f)
			+ FVector::DotProduct(Direction, DesiredMoveDirection) * 8.0f + Free * 0.01f;
		if (Score > BestWalkScore)
		{
			BestWalkScore = Score;
			BestWalk = Direction;
			BestWalkClearance = Clearance;
		}
	}
	const float CommitSeconds = FMath::Clamp(Contact + 0.12f, 0.15f, 0.6f);
	if (BestWalkClearance >= EvasionSafeMargin * 0.5f)
	{
		EvadeDirection = BestWalk;
		EvadeUntil = Now + CommitSeconds;
		return true;
	}

	// Walking is too slow. A dash is invulnerable while it lasts, but stamina barely
	// regenerates, so it is only spent once the ball is about to arrive.
	if (Skill > 0.25f && Self->CanDashNow())
	{
		FVector BestDash = FVector::ZeroVector;
		float BestDashClearance = -TNumericLimits<float>::Max();
		for (int32 Candidate = 0; Candidate < 12; ++Candidate)
		{
			const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 30.0f, FVector::UpVector);
			const float Free = GetFreeTravel(Location, Direction, 600.0f);
			if (Free < Self->GetDashDistance() * 0.6f)
			{
				continue;
			}
			const float Clearance = EvaluateEscape(Self, Threats, Direction, Free, true, false);
			if (Clearance > BestDashClearance)
			{
				BestDashClearance = Clearance;
				BestDash = Direction;
			}
		}
		if (!BestDash.IsNearlyZero() && BestDashClearance > BestWalkClearance
			&& Contact <= FMath::Lerp(0.2f, 0.3f, Skill))
		{
			Self->RequestAIDash(BestDash);
			EvadeDirection = BestDash;
			EvadeUntil = Now + Self->GetDashDuration() + 0.25f;
			return true;
		}
	}

	if (!Self->GetCharacterMovement()->IsFalling() && Contact < 0.35f)
	{
		const float JumpClearance = EvaluateEscape(Self, Threats, BestWalk,
			GetFreeTravel(Location, BestWalk, 700.0f), false, true);
		if (JumpClearance > BestWalkClearance + 10.0f)
		{
			Self->Jump();
			bHoldingJump = true;
			StopJumpAt = Now + 0.2f;
		}
	}
	EvadeDirection = BestWalk;
	EvadeUntil = Now + CommitSeconds;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Offence

AChaosImpactCharacter* AChaosImpactCPUController::SelectTarget(const AChaosImpactCharacter* Self) const
{
	AChaosImpactCharacter* Best = nullptr;
	float BestScore = -TNumericLimits<float>::Max();
	const FVector Location = Self->GetActorLocation();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Candidate = *It;
		if (Candidate == Self || Candidate->IsEliminated())
		{
			continue;
		}
		float Score = -FVector::Dist2D(Location, Candidate->GetActorLocation()) / 900.0f;
		Score += (Candidate->GetMaxHealth() - Candidate->GetHealth()) * 0.35f;
		Score += Candidate->GetStamina() < 1.0f ? 0.45f : 0.0f;
		Score += Candidate->IsChargingThrow() ? 0.2f : 0.0f;
		Score += LineOfSightTo(Candidate) ? 0.5f : 0.0f;
		// Stick with the current target unless another is clearly better, and focus human players.
		Score += Candidate == CurrentTarget.Get() ? 0.6f : 0.0f;
		Score -= Cast<AChaosImpactCPUController>(Candidate->GetController()) ? 0.3f : 0.0f;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	return Best;
}

float AChaosImpactCPUController::FindShotArrival(const TArray<FVector>& Path, const float StepSeconds,
	const AChaosImpactCharacter* Target, const FVector& TargetVelocity, const float LaunchDelay) const
{
	const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	// Slightly tighter than the real contact radius so near misses are not counted as hits.
	const float HitRadius = Capsule->GetScaledCapsuleRadius() + BallRadius * 0.6f;
	const FVector TargetLocation = Target->GetActorLocation();
	for (int32 Index = 0; Index < Path.Num(); ++Index)
	{
		const float T = Index * StepSeconds;
		const FVector Body = TargetLocation + TargetVelocity * (LaunchDelay + T);
		const FVector& Ball = Path[Index];
		if (FMath::Abs(Ball.Z - Body.Z) <= HalfHeight + 18.0f && FVector::Dist2D(Ball, Body) < HitRadius)
		{
			return T;
		}
	}
	return -1.0f;
}

AChaosImpactCPUController::FShotPlan AChaosImpactCPUController::PlanShot(
	const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target,
	const float ChargeAlpha, const bool bAllowBank) const
{
	FShotPlan Plan;
	if (!Self || !Target || !GetWorld())
	{
		return Plan;
	}
	const bool bArc = UsesArcFlightMode();
	const float Speed = Self->GetThrowSpeedForCharge(ChargeAlpha, bArc);
	const float Delay = Self->GetThrowReleaseDelay();
	const float SelfFeet = Self->GetActorLocation().Z
		- Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Origin = Self->GetActorLocation() + GetObservedVelocity(Self).GetSafeNormal2D()
		* FMath::Min(GetObservedVelocity(Self).Size2D(), 600.0f) * Delay;
	Origin.Z = SelfFeet + LearnedReleaseHeight;

	FVector TargetVelocity = GetObservedVelocity(Target);
	TargetVelocity.Z = 0.0f;
	TargetVelocity = TargetVelocity.GetClampedToMaxSize(1200.0f);
	const FVector TargetLocation = Target->GetActorLocation();

	// Solve for the interception point: the target keeps moving during release and flight.
	FVector Predicted = TargetLocation;
	float Flight = FVector::Dist2D(Origin, Predicted) / Speed;
	for (int32 Iteration = 0; Iteration < 5; ++Iteration)
	{
		Predicted = TargetLocation + TargetVelocity * (Delay + Flight);
		Flight = FVector::Dist2D(Origin, Predicted) / Speed;
	}
	const FVector Direction = (Predicted - Origin).GetSafeNormal2D()
		.RotateAngleAxis(ShotAimErrorDegrees, FVector::UpVector);

	TArray<FVector> Path;
	const float Horizon = FMath::Min(Flight + 0.4f, 1.6f);
	SimulateBallPath(Origin, Direction * Speed, bArc, Horizon, ShotStepSeconds, Self, Target, Path);
	float Arrival = FindShotArrival(Path, ShotStepSeconds, Target, TargetVelocity, Delay);
	if (Arrival >= 0.0f)
	{
		Plan.bValid = true;
		Plan.Direction = Direction;
	}
	else if (bAllowBank)
	{
		// Search reflections off nearby walls when the direct line is blocked or out of reach.
		float BestArrival = TNumericLimits<float>::Max();
		for (int32 Candidate = -11; Candidate <= 11; ++Candidate)
		{
			if (Candidate == 0)
			{
				continue;
			}
			const FVector BankDirection = Direction.RotateAngleAxis(Candidate * 7.5f, FVector::UpVector);
			SimulateBallPath(Origin, BankDirection * Speed, bArc, 1.4f, ShotStepSeconds * 1.5f,
				Self, Target, Path);
			const float BankArrival = FindShotArrival(Path, ShotStepSeconds * 1.5f, Target, TargetVelocity, Delay);
			if (BankArrival >= 0.0f && BankArrival < BestArrival)
			{
				BestArrival = BankArrival;
				Plan.bValid = true;
				Plan.bBankShot = true;
				Plan.Direction = BankDirection;
			}
		}
		Arrival = BestArrival;
	}
	if (!Plan.bValid)
	{
		return Plan;
	}
	Plan.ArrivalSeconds = Delay + Arrival;

	// Shot quality: how little time the target has compared with a human reaction and sidestep.
	const UCapsuleComponent* TargetCapsule = Target->GetCapsuleComponent();
	const float TargetWalk = FMath::Max(Target->GetCharacterMovement()->MaxWalkSpeed, 1.0f);
	const float SidestepSeconds = (TargetCapsule->GetScaledCapsuleRadius() + BallRadius + 12.0f) / TargetWalk;
	float Quality = FMath::Clamp(
		(OpponentReactionSeconds + SidestepSeconds + 0.08f - Plan.ArrivalSeconds) / 0.35f, 0.0f, 1.0f);
	Quality += Target->GetStamina() < 1.0f ? 0.25f : 0.0f;
	Quality += Target->GetCharacterMovement()->IsFalling() ? 0.35f : 0.0f;
	Quality += Target->IsDashing() ? 0.2f : 0.0f;
	Quality += Plan.bBankShot ? 0.15f : 0.0f;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Plan.Direction);
	if (GetFreeTravel(TargetLocation, Side, 160.0f, Target) < 110.0f
		&& GetFreeTravel(TargetLocation, -Side, 160.0f, Target) < 110.0f)
	{
		// Cornered or in a corridor: sidestepping is not an option.
		Quality += 0.3f;
	}
	Plan.Quality = Quality;
	return Plan;
}

float AChaosImpactCPUController::ChooseDesiredCharge(
	const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target) const
{
	const bool bArc = UsesArcFlightMode();
	const float MinSpeed = Self->GetThrowSpeedForCharge(0.0f, bArc);
	const float MaxSpeed = Self->GetThrowSpeedForCharge(1.0f, bArc);
	const auto AlphaForSpeed = [MinSpeed, MaxSpeed](const float Speed)
	{
		return (Speed - MinSpeed) / FMath::Max(MaxSpeed - MinSpeed, 1.0f);
	};
	const float Distance = FVector::Dist2D(Self->GetActorLocation(), Target->GetActorLocation());
	// Aim for a flight short enough that a reacting human cannot sidestep in time.
	float Desired = AlphaForSpeed(Distance / 0.3f);
	if (bArc)
	{
		Desired = FMath::Max(Desired, AlphaForSpeed((Distance - 40.0f) / GetArcFlightLimitSeconds()) + 0.08f);
	}
	if (Target->GetStamina() < 1.0f || Target->GetCharacterMovement()->IsFalling())
	{
		// They cannot dash away, so a quicker release matters more than ball speed.
		Desired *= 0.8f;
	}
	return FMath::Clamp(Desired, 0.12f, 1.0f);
}

void AChaosImpactCPUController::UpdateOffense(
	AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, const float Now)
{
	const int32 Balls = Self->GetCarriedBallCount();
	if (!Target || Balls <= 0)
	{
		if (Self->IsChargingThrow())
		{
			Self->CancelChargingThrow();
		}
		return;
	}
	const bool bBankSearch = Skill >= 0.45f && Now >= NextBankSearchAt;

	if (!Self->IsChargingThrow())
	{
		if (Now < NextThrowAt || Self->IsDashing() || Self->IsThrowReleasePending())
		{
			return;
		}
		DesiredChargeAlpha = ChooseDesiredCharge(Self, Target);
		ShotAimErrorDegrees = FMath::FRandRange(-1.0f, 1.0f) * FMath::Lerp(7.0f, 0.6f, Skill);
		const FShotPlan Preview = PlanShot(Self, Target, DesiredChargeAlpha, bBankSearch);
		if (bBankSearch)
		{
			NextBankSearchAt = Now + 0.3f;
		}
		const float Distance = FVector::Dist2D(Self->GetActorLocation(), Target->GetActorLocation());
		if (Preview.bValid || Distance < 900.0f)
		{
			Self->BeginThrowInput();
			if (Self->IsChargingThrow())
			{
				ChargeStartedAt = Now;
				LastValidShotAt = Now;
				if (Preview.bValid)
				{
					Self->SetAIAimDirection(Preview.Direction);
				}
			}
		}
		return;
	}

	// While charging, keep re-solving for the speed the ball would have if released now, so the
	// aim stays correct as the target moves and as the charge (and therefore flight time) grows.
	const float Alpha = Self->GetThrowChargeAlpha();
	const FShotPlan Plan = PlanShot(Self, Target, Alpha, bBankSearch);
	if (bBankSearch)
	{
		NextBankSearchAt = Now + 0.2f;
	}
	if (Plan.bValid)
	{
		LastValidShotAt = Now;
		Self->SetAIAimDirection(Plan.Direction);
	}

	const float Held = Now - ChargeStartedAt;
	const float Patience = FMath::Clamp(
		(Held - Self->GetMaxChargeSeconds() * DesiredChargeAlpha) / 1.2f, 0.0f, 1.0f);
	const float RequiredQuality = FMath::Lerp(0.55f, 0.05f, Patience);
	const bool bChargeReady = Alpha + 0.001f >= DesiredChargeAlpha || Alpha >= 0.999f;
	// A dashing or airborne target is committed to its path; punish it immediately.
	const bool bPunish = (Target->IsDashing() || Target->GetCharacterMovement()->IsFalling()) && Alpha >= 0.2f;
	if (Plan.bValid && ((bChargeReady && Plan.Quality >= RequiredQuality) || bPunish))
	{
		Self->EndThrowInput();
		// With a second ball, follow up quickly to catch the dodge of the first.
		NextThrowAt = Now + (Balls > 1 ? FMath::Lerp(0.6f, 0.26f, Skill) : FMath::Lerp(0.9f, 0.45f, Skill));
		return;
	}
	if (Alpha >= 0.999f && Now - LastValidShotAt > 1.1f)
	{
		Self->CancelChargingThrow();
		NextThrowAt = Now + 0.25f;
	}
}

// ---------------------------------------------------------------------------------------------
// Positioning and ball economy

AChaosImpactBall* AChaosImpactCPUController::SelectPickup(const AChaosImpactCharacter* Self) const
{
	AChaosImpactBall* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const FVector Location = Self->GetActorLocation();
	for (TActorIterator<AChaosImpactBall> BallIt(GetWorld()); BallIt; ++BallIt)
	{
		AChaosImpactBall* Ball = *BallIt;
		if (!Ball->IsPickup())
		{
			continue;
		}
		const FVector BallLocation = Ball->GetActorLocation();
		const float MyDistance = FVector::Dist2D(Location, BallLocation);
		float Score = MyDistance + (Ball->IsPickupAvailable() ? 0.0f : 150.0f);
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (*It == Self || It->IsEliminated())
			{
				continue;
			}
			const float TheirDistance = FVector::Dist2D(It->GetActorLocation(), BallLocation);
			// Skip races we will lose, and balls guarded by an armed opponent.
			if (TheirDistance < MyDistance)
			{
				Score += (MyDistance - TheirDistance) * 0.8f;
			}
			if (It->GetCarriedBallCount() > 0 && TheirDistance < 900.0f)
			{
				Score += (900.0f - TheirDistance) / 900.0f * 600.0f;
			}
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Ball;
		}
	}
	return Best;
}

bool AChaosImpactCPUController::TryCollectNearbyBall(AChaosImpactCharacter* Self, const float Now)
{
	if (Self->GetCarriedBallCount() >= Self->GetMaximumCarriedBalls())
	{
		return false;
	}
	const FVector Location = Self->GetActorLocation();
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		if (Ball->IsPickupAvailable() && FVector::Dist2D(Ball->GetActorLocation(), Location) <= PickupReach
			&& Self->TryPickupBall(Ball))
		{
			Ball->Destroy();
			NextThrowAt = FMath::Max(NextThrowAt, Now + FMath::Lerp(0.45f, 0.12f, Skill));
			return true;
		}
	}
	return false;
}

void AChaosImpactCPUController::UpdatePositioning(
	AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, const float Now)
{
	const FVector Location = Self->GetActorLocation();
	const int32 Balls = Self->GetCarriedBallCount();
	FVector Desired = FVector::ZeroVector;
	float Scale = 0.95f;

	AChaosImpactBall* Pickup = Balls < Self->GetMaximumCarriedBalls() ? SelectPickup(Self) : nullptr;
	const float PickupDistance = Pickup ? FVector::Dist2D(Pickup->GetActorLocation(), Location) : 0.0f;
	const float TargetDistance = Target ? FVector::Dist2D(Target->GetActorLocation(), Location) : 0.0f;
	// Always hold a ball; take a second one when it is close and nobody is pressing.
	const bool bGoForPickup = Pickup && (Balls == 0
		|| (PickupDistance < 420.0f && (!Target || TargetDistance > 700.0f) && !Self->IsChargingThrow()));

	if (bGoForPickup)
	{
		const float LeadSeconds = FMath::Clamp(PickupDistance / 1800.0f, 0.05f, 0.28f);
		const FVector PredictedPickup = Pickup->GetActorLocation() + Pickup->GetBallVelocity() * LeadSeconds;
		Desired = (PredictedPickup - Location).GetSafeNormal2D();
		Scale = FMath::Clamp(PickupDistance / 260.0f, 0.38f, 1.0f);
	}
	else if (Target)
	{
		const FVector TowardTarget = (Target->GetActorLocation() - Location).GetSafeNormal2D();
		const bool bArc = UsesArcFlightMode();
		const bool bTargetArmed = Target->GetCarriedBallCount() > 0;
		float Ideal = bArc
			? FMath::Clamp(Self->GetThrowSpeedForCharge(1.0f, true) * GetArcFlightLimitSeconds() * 0.55f, 600.0f, 1050.0f)
			: 900.0f;
		if (Balls == 0)
		{
			// Unarmed with no reachable ball: stay out of reach.
			Ideal = bTargetArmed ? 1400.0f : 1000.0f;
		}
		else if (!bTargetArmed)
		{
			// Press an unarmed opponent: short range leaves no time to react.
			Ideal *= 0.72f;
		}
		if (Self->GetHealth() <= 1.0f && bTargetArmed)
		{
			Ideal *= 1.25f;
		}
		float Radial = FMath::Clamp((TargetDistance - Ideal) / 320.0f, -1.0f, 1.0f);
		const bool bHasSight = LineOfSightTo(Target);
		if (!bHasSight && Balls > 0)
		{
			// Walk around cover to open an angle.
			Radial = FMath::Max(Radial, 0.35f);
		}

		// Irregular strafing makes the CPU hard to lead.
		if (Now >= NextStrafeChangeAt)
		{
			StrafeSign *= -1.0f;
			NextStrafeChangeAt = Now + FMath::FRandRange(0.7f, 1.9f);
		}
		FVector Strafe = FVector::CrossProduct(FVector::UpVector, TowardTarget) * StrafeSign;
		if (!IsPathClear(Location, Strafe, 260.0f))
		{
			StrafeSign *= -1.0f;
			Strafe *= -1.0f;
			NextStrafeChangeAt = Now + 0.8f;
		}
		Desired = (TowardTarget * Radial + Strafe * (bHasSight ? 0.9f : 1.2f)).GetSafeNormal2D();
	}
	else if (FVector::Dist2D(Location, HomeLocation) > 250.0f)
	{
		Desired = (HomeLocation - Location).GetSafeNormal2D();
		Scale = 0.6f;
	}

	if (Desired.IsNearlyZero())
	{
		DesiredMoveDirection = FVector::ZeroVector;
		DesiredMoveScale = 0.0f;
		return;
	}
	const FVector AvoidedDirection = AvoidNearbyObstacle(Location, Desired);
	DesiredMoveDirection = FMath::VInterpTo(DesiredMoveDirection,
		AvoidedDirection, 0.07f, 8.5f).GetSafeNormal2D();
	DesiredMoveScale = Scale;
	TryTraverseObstacle(Self, DesiredMoveDirection, Now);
}

bool AChaosImpactCPUController::UsesArcFlightMode() const
{
	for (TActorIterator<AChaosImpactPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsPrimaryLocalPlayerController())
		{
			return It->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
		}
	}
	return !GetWorld() || !GetWorld()->URL.HasOption(TEXT("CIBallStraight=1"));
}

// ---------------------------------------------------------------------------------------------
// Navigation

FVector AChaosImpactCPUController::AvoidNearbyObstacle(
	const FVector& Origin, const FVector& DesiredDirection) const
{
	const FVector Forward = DesiredDirection.GetSafeNormal2D();
	if (!GetWorld() || Forward.IsNearlyZero())
	{
		return Forward;
	}
	// Low cover is intentionally traversed instead of avoided; TryTraverseObstacle
	// supplies the jump input. Taller geometry is evaluated with a wide steering fan.
	if (HasLowObstacleAhead(Origin, Forward))
	{
		return Forward;
	}
	if (IsPathClear(Origin, Forward, 430.0f))
	{
		return Forward;
	}

	static constexpr float CandidateAngles[] =
	{
		32.0f, -32.0f, 58.0f, -58.0f, 88.0f, -88.0f, 125.0f, -125.0f, 180.0f
	};
	FVector BestDirection = -Forward;
	float BestScore = -TNumericLimits<float>::Max();
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUObstacle), false, GetPawn());
	const FVector SweepStart = Origin + FVector::UpVector * 68.0f;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(38.0f, 62.0f);
	for (const float Angle : CandidateAngles)
	{
		const FVector Candidate = Forward.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal2D();
		FHitResult Hit;
		const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, SweepStart,
			SweepStart + Candidate * 430.0f, FQuat::Identity, ECC_Visibility, Shape, Parameters);
		const float Clearance = bBlocked ? Hit.Time : 1.0f;
		const float Alignment = FVector::DotProduct(Candidate, Forward);
		const float PreferredSide = FMath::Sign(Angle) == FMath::Sign(StrafeSign) ? 0.12f : 0.0f;
		const float Score = Clearance * 3.2f + Alignment * 1.65f + PreferredSide;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestDirection = Candidate;
		}
	}
	return BestDirection;
}

bool AChaosImpactCPUController::IsPathClear(
	const FVector& Origin, const FVector& Direction, const float Distance) const
{
	return !Direction.IsNearlyZero() && GetFreeTravel(Origin, Direction, Distance) >= Distance;
}

float AChaosImpactCPUController::GetFreeTravel(const FVector& Origin, const FVector& Direction,
	const float MaxDistance, const AActor* IgnoredActor) const
{
	if (!GetWorld() || Direction.IsNearlyZero())
	{
		return 0.0f;
	}
	FHitResult Hit;
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUClearance), false, GetPawn());
	Parameters.AddIgnoredActor(IgnoredActor);
	const FVector Start = Origin + FVector::UpVector * 68.0f;
	return GetWorld()->SweepSingleByChannel(Hit, Start,
		Start + Direction.GetSafeNormal2D() * MaxDistance, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeCapsule(38.0f, 62.0f), Parameters)
		? Hit.Time * MaxDistance : MaxDistance;
}

void AChaosImpactCPUController::TryTraverseObstacle(
	AChaosImpactCharacter* ControlledCharacter, const FVector& Direction, const float Now)
{
	if (!ControlledCharacter || ControlledCharacter->IsDashing()
		|| Now < NextJumpAt || Direction.IsNearlyZero()
		|| !HasLowObstacleAhead(ControlledCharacter->GetActorLocation(), Direction))
	{
		return;
	}
	ControlledCharacter->Jump();
	bHoldingJump = true;
	StopJumpAt = Now + 0.18f;
	NextJumpAt = Now + FMath::FRandRange(1.6f, 2.8f);
}

void AChaosImpactCPUController::UpdateStuckRecovery(
	AChaosImpactCharacter* ControlledCharacter, const float Now)
{
	if (!ControlledCharacter || Now - LastProgressCheckAt < 0.55f)
	{
		return;
	}
	const FVector Location = ControlledCharacter->GetActorLocation();
	const float Progress = FVector::Dist2D(Location, LastProgressLocation);
	const bool bWasTryingToMove = DesiredMoveScale > 0.25f && Now >= EvadeUntil
		&& !DesiredMoveDirection.IsNearlyZero() && !ControlledCharacter->IsDashing();
	if (bWasTryingToMove && Progress < 34.0f)
	{
		StrafeSign *= -1.0f;
		const FVector SideStep = DesiredMoveDirection.RotateAngleAxis(
			StrafeSign * 92.0f, FVector::UpVector).GetSafeNormal2D();
		EscapeMoveDirection = AvoidNearbyObstacle(Location, SideStep);
		if (EscapeMoveDirection.IsNearlyZero())
		{
			EscapeMoveDirection = -DesiredMoveDirection;
		}
		EscapeUntil = Now + 0.85f;
		TryTraverseObstacle(ControlledCharacter, EscapeMoveDirection, Now);
	}
	LastProgressLocation = Location;
	LastProgressCheckAt = Now;
}

bool AChaosImpactCPUController::HasLowObstacleAhead(
	const FVector& Origin, const FVector& DesiredDirection) const
{
	if (!GetWorld() || DesiredDirection.IsNearlyZero())
	{
		return false;
	}
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUJump), false, GetPawn());
	FHitResult LowHit;
	FHitResult HighHit;
	const FVector LowStart = Origin + FVector::UpVector * 32.0f;
	const FVector HighStart = Origin + FVector::UpVector * 125.0f;
	const FVector Offset = DesiredDirection * 150.0f;
	const bool bLowBlocked = GetWorld()->LineTraceSingleByChannel(
		LowHit, LowStart, LowStart + Offset, ECC_Visibility, Parameters);
	const bool bHighBlocked = GetWorld()->LineTraceSingleByChannel(
		HighHit, HighStart, HighStart + Offset, ECC_Visibility, Parameters);
	return bLowBlocked && !bHighBlocked;
}

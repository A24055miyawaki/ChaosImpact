#include "ChaosImpactCPUController.h"

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTornado.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

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
	// VS matches rank CPUs and put them in teams like everyone else.
	bWantsPlayerState = true;
}

void AChaosImpactCPUController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (AChaosImpactCharacter* Placed = Cast<AChaosImpactCharacter>(InPawn); Placed && !bMatchCPU)
	{
		// A character placed in a level: its own settings say how strong, and placed enemies stand together.
		SetDifficulty(static_cast<int32>(Placed->CPULevel));
		if (Placed->SoloTeam < 0)
		{
			Placed->SoloTeam = AChaosImpactCharacter::SoloEnemyTeam;
		}
	}
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	NextDecisionAt = Now + 0.2f;
	NextThrowAt = Now + 0.9f * ThrowDelayScale;
	NextBankSearchAt = Now;
	IdleUntil = 0.0f;
	UnnoticedBalls.Reset();
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
	SimaePerchedSince = -1.0;
	SimaeDashAt = -1.0;
}

double AChaosImpactCPUController::DevThinkSeconds = 0.0;
double AChaosImpactCPUController::DevSectionSeconds[6] = {};

namespace
{
	/** The stage's floor as metre squares a body can stand in (no wall there at body height). */
	struct FChaosImpactCPUNavGrid
	{
		FVector Origin = FVector::ZeroVector;
		float Cell = 100.0f;
		int32 NX = 0;
		int32 NY = 0;
		TArray<uint8> Blocked;
		FVector BuiltCenter = FVector::ZeroVector;
		float BuiltHalf = 0.0f;

		bool IsInside(const FIntPoint& C) const { return C.X >= 0 && C.Y >= 0 && C.X < NX && C.Y < NY; }
		int32 Index(const FIntPoint& C) const { return C.Y * NX + C.X; }
		FIntPoint CellOf(const FVector& P) const
		{
			return FIntPoint(FMath::FloorToInt((P.X - Origin.X) / Cell), FMath::FloorToInt((P.Y - Origin.Y) / Cell));
		}
		FVector CenterOf(const FIntPoint& C) const
		{
			return FVector(Origin.X + (C.X + 0.5f) * Cell, Origin.Y + (C.Y + 0.5f) * Cell, Origin.Z);
		}
		bool IsFree(const FIntPoint& C) const { return IsInside(C) && Blocked[Index(C)] == 0; }
	};

	TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FChaosImpactCPUNavGrid>>& GetCPUNavGrids()
	{
		static TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FChaosImpactCPUNavGrid>> Grids;
		return Grids;
	}

	/** The VS stage's grid, built the first time any CPU needs it (a few thousand overlap tests, once). */
	const FChaosImpactCPUNavGrid* FindCPUNavGrid(UWorld* World, const FVector& BodyLocation, const AActor* Ignored)
	{
		const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
		if (!Match || !Match->bVersusMatch)
		{
			return nullptr;
		}
		const FVector Center = Match->StageCenter;
		const float Half = Match->StageHalfExtent + 100.0f;
		TSharedPtr<FChaosImpactCPUNavGrid>& Grid = GetCPUNavGrids().FindOrAdd(World);
		if (Grid.IsValid() && Grid->BuiltCenter.Equals(Center, 1.0f) && FMath::IsNearlyEqual(Grid->BuiltHalf, Half, 1.0f))
		{
			return Grid.Get();
		}
		// Forget grids of worlds that are gone.
		for (auto It = GetCPUNavGrids().CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid())
			{
				It.RemoveCurrent();
			}
		}
		TSharedPtr<FChaosImpactCPUNavGrid>& Fresh = GetCPUNavGrids().FindOrAdd(World);
		Fresh = MakeShared<FChaosImpactCPUNavGrid>();
		Fresh->BuiltCenter = Center;
		Fresh->BuiltHalf = Half;
		Fresh->Origin = FVector(Center.X - Half, Center.Y - Half, BodyLocation.Z);
		Fresh->NX = Fresh->NY = FMath::CeilToInt(Half * 2.0f / Fresh->Cell);
		Fresh->Blocked.SetNumZeroed(Fresh->NX * Fresh->NY);
		FCollisionObjectQueryParams WallObjects;
		WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUNavGrid), false, Ignored);
		// The same body as the CPU's own wall checks (SweepWalls), a little wider so a route keeps off the corners.
		const FCollisionShape Body = FCollisionShape::MakeCapsule(44.0f, 62.0f);
		for (int32 Y = 0; Y < Fresh->NY; ++Y)
		{
			for (int32 X = 0; X < Fresh->NX; ++X)
			{
				const FVector At = Fresh->CenterOf(FIntPoint(X, Y)) + FVector::UpVector * 68.0f;
				Fresh->Blocked[Y * Fresh->NX + X] = World->OverlapAnyTestByObjectType(At, FQuat::Identity, WallObjects, Body, Parameters) ? 1 : 0;
			}
		}
		return Fresh.Get();
	}
}

void AChaosImpactCPUController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double ThinkStartedAt = FPlatformTime::Seconds();
	ON_SCOPE_EXIT { DevThinkSeconds += FPlatformTime::Seconds() - ThinkStartedAt; };
	AChaosImpactCharacter* Self = Cast<AChaosImpactCharacter>(GetPawn());
	if (!Self || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	double SectionAt = FPlatformTime::Seconds();
	const auto Section = [&SectionAt](const int32 Index)
	{
		const double Now2 = FPlatformTime::Seconds();
		DevSectionSeconds[Index] += Now2 - SectionAt;
		SectionAt = Now2;
	};
	UpdatePerception(Self, DeltaSeconds, Now);
	Section(0);
	if (bPerfect)
	{
		UpdateLearning(Self, Now);
	}
	Section(1);
	if (Self->IsEliminated())
	{
		EvadeUntil = 0.0f;
		return;
	}
	if (Self->IsTrainingMenuFrozen() || Self->IsMatchInputLocked())
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
	if (Self->IsDriving())
	{
		// Steering its drive ball: at whoever it is after, where they will be when the ball gets there. The thrower
		// stands rooted meanwhile.
		if (AChaosImpactBall* Driven = Self->GetDrivenBall())
		{
			AChaosImpactCharacter* Target = CurrentTarget.IsValid() && !CurrentTarget->IsEliminated()
				? CurrentTarget.Get() : SelectTarget(Self);
			if (Target)
			{
				const FVector BallAt = Driven->GetActorLocation();
				const float Speed = FMath::Max(static_cast<float>(Driven->GetVelocity().Size2D()), 1.0f);
				const float Arrival = FMath::Min(static_cast<float>(FVector::Dist2D(BallAt, Target->GetActorLocation())) / Speed, 1.0f);
				const FVector Aim = Target->GetActorLocation() + GetObservedVelocity(Target) * Arrival * LeadScale;
				Self->SetAIDriveSteer((Aim - BallAt).GetSafeNormal2D());
			}
		}
		DesiredMoveDirection = FVector::ZeroVector;
		DesiredMoveScale = 0.0f;
		return;
	}
	if (Self->IsCarriedByWind())
	{
		// Whirled round a tornado: nothing to do until it lets go.
		if (Self->IsChargingThrow())
		{
			Self->CancelChargingThrow();
		}
		EvadeUntil = 0.0f;
		TornadoEvadeUntil = 0.0f;
		return;
	}

	if (AChaosImpactSimaeBird::HasPerchedBird(Self))
	{
		if (SimaePerchedSince < 0.0)
		{
			SimaePerchedSince = Now;
			const float Reaction = bPerfect ? FMath::FRandRange(0.03f, 0.1f)
				: Difficulty == ChaosImpactMatch::CPULevelWeak ? FMath::FRandRange(2.15f, 2.7f)
				: Difficulty == ChaosImpactMatch::CPULevelNormal ? FMath::FRandRange(0.8f, 1.35f)
				: FMath::FRandRange(0.35f, 0.7f);
			SimaeDashAt = Now + Reaction;
		}
		if (Now >= SimaeDashAt && Self->CanDashNow())
		{
			const FVector ShakeDirection = DesiredMoveDirection.IsNearlyZero()
				? Self->GetActorForwardVector() : DesiredMoveDirection;
			Self->RequestAIDash(ShakeDirection);
			SimaeDashAt = Now + 0.25;
		}
	}
	else
	{
		SimaePerchedSince = -1.0;
		SimaeDashAt = -1.0;
	}

	if (Now >= NextDecisionAt)
	{
		// さいきょう decides 40 times a second (a reaction well inside a frame or two; every frame was needlessly heavy).
		const float DecisionSeconds = bPerfect ? PerfectDecisionSeconds : FMath::Lerp(0.12f, 0.045f, Skill);
		NextDecisionAt = Now + DecisionSeconds;
		if (Now >= IdleUntil && IdleChancePerSecond > 0.0f && FMath::FRand() < IdleChancePerSecond * DecisionSeconds)
		{
			// A weaker CPU stands around for a moment, as a beginner would: no plans, no dodging.
			IdleUntil = Now + FMath::FRandRange(0.7f, 1.6f);
			if (Self->IsChargingThrow())
			{
				Self->CancelChargingThrow();
			}
			DesiredMoveDirection = FVector::ZeroVector;
			DesiredMoveScale = 0.0f;
			EvadeUntil = 0.0f;
		}
		if (Now >= IdleUntil)
		{
			SectionAt = FPlatformTime::Seconds();
			AChaosImpactCharacter* Target = SelectTarget(Self);
			CurrentTarget = Target;
			TryCollectNearbyBall(Self, Now);
			Section(2);
			const bool bEvading = UpdateTornadoEvasion(Self, Now) || UpdateEvasion(Self, Now);
			Section(3);
			UpdateOffense(Self, Target, Now);
			Section(4);
			if (!bEvading)
			{
				UpdatePositioning(Self, Target, Now);
			}
			Section(5);
		}
	}
	if (Now >= IdleUntil)
	{
		UpdateStuckRecovery(Self, Now);
	}
#if !UE_BUILD_SHIPPING
	static const bool bDevTrace = FParse::Param(FCommandLine::Get(), TEXT("CICPUTrace"));
	if (bDevTrace && bPerfect && Now >= DevTraceAt)
	{
		DevTraceAt = Now + 1.0f;
		const AChaosImpactCharacter* Target = CurrentTarget.Get();
		UE_LOG(LogTemp, Log, TEXT("CPUTRACE balls=%d charging=%d hunting=%d evading=%d mash=%d dist=%.0f stamina=%.1f pickupsOnFloor=%d move=%s"),
			Self->GetCarriedBallCount(), Self->IsChargingThrow(), bHunting, Now < EvadeUntil, bSnowMashing,
			Target ? FVector::Dist2D(Target->GetActorLocation(), Self->GetActorLocation()) : -1.0f, Self->GetStamina(),
			[this]() { int32 N = 0; for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It) { N += It->IsPickupAvailable() ? 1 : 0; } return N; }(),
			*DesiredMoveDirection.ToCompactString());
		UE_LOG(LogTemp, Log, TEXT("CPUTRACE at %s target %s vantage %s pickup %s"), *Self->GetActorLocation().ToCompactString(),
			Target ? *Target->GetActorLocation().ToCompactString() : TEXT("-"), *VantagePoint.ToCompactString(),
			[this, Self]() { const AChaosImpactBall* P = SelectPickup(Self); return P ? P->GetActorLocation().ToCompactString() : FString(TEXT("-")); }().GetCharArray().GetData());
	}
#endif

	// Decisions are throttled, but movement input must be supplied every frame.
	// Otherwise CharacterMovement consumes it and the CPU visibly stutters.
	const bool bEvading = Now < EvadeUntil && !EvadeDirection.IsNearlyZero();
	if (bSnowMashing && !bEvading && Now >= EscapeUntil && !Self->IsDashing() && !SnowMashAxis.IsNearlyZero())
	{
		// Waggling: the way flips faster than the snowball's own limit on how often a flip counts.
		if (Now >= NextSnowMashFlipAt)
		{
			SnowMashSign = -SnowMashSign;
			NextSnowMashFlipAt = Now + 0.085f;
		}
		Self->AddMovementInput(SnowMashAxis * SnowMashSign, 1.0f);
		return;
	}
	const FVector ActiveMoveDirection = bEvading ? EvadeDirection
		: Now < EscapeUntil ? EscapeMoveDirection : DesiredMoveDirection;
	if (!Self->IsDashing() && !ActiveMoveDirection.IsNearlyZero())
	{
		Self->AddMovementInput(ActiveMoveDirection, (bEvading ? 1.0f : DesiredMoveScale) * MoveSpeedScale);
	}
}

void AChaosImpactCPUController::SetDifficulty(const int32 Level)
{
	Difficulty = ChaosImpactMatch::SanitizeCPULevel(Level);
	bPerfect = Difficulty == ChaosImpactMatch::CPULevelStrongest;
	switch (Difficulty)
	{
	case ChaosImpactMatch::CPULevelWeak:
		// For beginners: slow to react, misses a lot, rarely dodges, never dashes, walks slowly, throws rarely.
		Skill = 0.0f;
		DodgeChance = 0.2f;
		ExtraReactionSeconds = 0.3f;
		ReleaseShakeDegrees = 22.0f;
		LeadScale = 0.25f;
		MoveSpeedScale = 0.6f;
		ThrowDelayScale = 3.2f;
		IdleChancePerSecond = 0.22f;
		break;
	case ChaosImpactMatch::CPULevelNormal:
		// A fair opponent: dodges most balls and aims decently, but can be outplayed.
		Skill = 0.45f;
		DodgeChance = 0.7f;
		ExtraReactionSeconds = 0.08f;
		ReleaseShakeDegrees = 5.0f;
		LeadScale = 0.75f;
		MoveSpeedScale = 0.9f;
		ThrowDelayScale = 1.3f;
		IdleChancePerSecond = 0.05f;
		break;
	default:
		// The full CPU, unchanged (さいきょう: the same, and plays exactly; see bPerfect).
		Skill = 1.0f;
		DodgeChance = 1.0f;
		ExtraReactionSeconds = 0.0f;
		ReleaseShakeDegrees = 0.0f;
		LeadScale = 1.0f;
		MoveSpeedScale = 1.0f;
		ThrowDelayScale = 1.0f;
		IdleChancePerSecond = 0.0f;
		break;
	}
	IdleUntil = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("%s plays at CPU level %s"), *GetName(), ChaosImpactMatch::GetCPULevelName(Difficulty));
}

FVector AChaosImpactCPUController::ShakeAim(const FVector& Direction) const
{
	return ReleaseShakeThisThrow == 0.0f ? Direction : Direction.RotateAngleAxis(ReleaseShakeThisThrow, FVector::UpVector);
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
		const FVector Previous = Smoothed;
		Smoothed = FMath::Lerp(Smoothed, Measured, Blend);
		// A player who is turning or starting to run keeps changing speed; leading with it lands the shot.
		FVector& Acceleration = ObservedAccelerations.FindOrAdd(Observed);
		Acceleration = FMath::Lerp(Acceleration, (Smoothed - Previous) / SafeDelta, Blend)
			.GetClampedToMaxSize(4000.0f);
		Acceleration.Z = 0.0f;
		LastObservedLocations.Add(Observed, Location);
	}

	for (auto It = BallFirstSeenAt.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Key()->IsPickup())
		{
			UnnoticedBalls.Remove(It.Key());
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
		if (!Ball->WasThrownBy(Self) && (Self->IsBlinded() || (DodgeChance < 1.0f && FMath::FRand() >= DodgeChance)))
		{
			// A weaker CPU simply does not see this one coming; nobody sees through smoke.
			UnnoticedBalls.Add(Ball);
		}
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

FVector AChaosImpactCPUController::GetObservedAcceleration(const AChaosImpactCharacter* Observed) const
{
	const FVector* Acceleration = ObservedAccelerations.Find(Observed);
	return Acceleration ? *Acceleration : FVector::ZeroVector;
}

float AChaosImpactCPUController::GetReactionSeconds() const
{
	// さいきょう sees a throw the moment it leaves the hand.
	return bPerfect ? 0.0f : FMath::Lerp(0.34f, 0.07f, Skill) + ExtraReactionSeconds;
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
	if (bPerfect)
	{
		CollectExactThreats(Self, OutThreats);
		return;
	}
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
		if (!SeenAt || Now - *SeenAt < Reaction || UnnoticedBalls.Contains(Ball))
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
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float WalkSpeed = Movement->MaxWalkSpeed;
	const float DashSeconds = FMath::Max(Self->GetDashDuration(), 0.01f);
	const float GravityZ = GetWorld()->GetGravityZ() * Movement->GravityScale;
	float Worst = TNumericLimits<float>::Max();
	if (bPerfect)
	{
		// Every moment of the way, clear of ground that is burning, or will be once a fire ball has passed over it.
		for (float T = 0.0f; T <= 0.8f; T += 0.1f)
		{
			float Travel = bDash ? (T < DashSeconds ? Self->GetDashDistance() * T / DashSeconds
				: Self->GetDashDistance() + WalkSpeed * (T - DashSeconds)) : WalkSpeed * T * (bJump ? 0.5f : 1.0f);
			const FVector Body = Start + Direction * FMath::Min(Travel, TravelLimit);
			for (const FVector4& Burn : ActiveBurns)
			{
				Worst = FMath::Min(Worst, static_cast<float>(FVector::Dist2D(FVector(Burn.X, Burn.Y, Burn.Z), Body))
					- static_cast<float>(Burn.W) - CapsuleRadius + GetSafeMargin());
			}
			for (const FBallThreat& Threat : Threats)
			{
				if (!Threat.bTrail)
				{
					continue;
				}
				for (int32 Index = 0; Index < Threat.Path.Num() && Index * Threat.StepSeconds <= T; Index += 4)
				{
					Worst = FMath::Min(Worst, static_cast<float>(FVector::Dist2D(Threat.Path[Index], Body))
						- (AChaosImpactHazardZone::FireTrailRadius + CapsuleRadius) + GetSafeMargin());
				}
			}
		}
	}
	for (const FBallThreat& Threat : Threats)
	{
		const float HitRadius = CapsuleRadius + Threat.HitRadius;
		const float HeightReach = Threat.HitHeight > 0.0f ? Threat.HitHeight : HalfHeight + Threat.HitRadius;
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
			// Where it bursts, everyone within its blast is hit, however it was dodged.
			if (Threat.bBursts && Threat.BurstRadius > 0.0f && Index == Threat.Path.Num() - 1)
			{
				Worst = FMath::Min(Worst, static_cast<float>(FVector::Dist2D(Ball, Body)) - (Threat.BurstRadius + CapsuleRadius));
			}
			if (FMath::Abs(Ball.Z - Body.Z) > HeightReach)
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
	float Earliest = TNumericLimits<float>::Max();
	for (const FBallThreat& Threat : Threats)
	{
		const float DangerRadius = Capsule->GetScaledCapsuleRadius() + Threat.HitRadius + GetSafeMargin();
		const float HeightReach = Threat.HitHeight > 0.0f ? Threat.HitHeight : HalfHeight + Threat.HitRadius;
		if (Threat.bBursts && Threat.BurstRadius > 0.0f && Threat.Path.Num() > 0
			&& FVector::Dist2D(Threat.Path.Last(), Location) < Threat.BurstRadius + Capsule->GetScaledCapsuleRadius() + GetSafeMargin())
		{
			Earliest = FMath::Min(Earliest, (Threat.Path.Num() - 1) * Threat.StepSeconds);
		}
		for (int32 Index = 0; Index < Threat.Path.Num(); ++Index)
		{
			const FVector& Ball = Threat.Path[Index];
			if (FMath::Abs(Ball.Z - Location.Z) <= HeightReach
				&& FVector::Dist2D(Ball, Location) < DangerRadius)
			{
				Earliest = FMath::Min(Earliest, Index * Threat.StepSeconds);
				break;
			}
		}
	}
	return Earliest;
}

bool AChaosImpactCPUController::UpdateTornadoEvasion(AChaosImpactCharacter* Self, const float Now)
{
	const FVector Location = Self->GetActorLocation();
	const float CapsuleRadius = Self->GetCapsuleComponent()->GetScaledCapsuleRadius();
	// Better CPUs look further ahead along its way.
	const float LookAhead = bPerfect ? 1.3f : FMath::Lerp(0.3f, 0.9f, Skill);
	const AChaosImpactTornado* Threat = nullptr;
	float ThreatMiss = TNumericLimits<float>::Max();
	float ThreatDistance = 0.0f;
	for (TActorIterator<AChaosImpactTornado> It(GetWorld()); It; ++It)
	{
		const AChaosImpactTornado* Tornado = *It;
		if (!Tornado->IsActive() || Tornado->GetSourcePawn() == Self
			|| AChaosImpactGameState::AreTeammates(GetWorld(), Tornado->GetSourcePawn(), Self))
		{
			continue;
		}
		const FVector Heading = Tornado->GetTravelDirection();
		const FVector Offset = Location - Tornado->GetCenter();
		const float Ahead = FMath::Clamp(static_cast<float>(FVector::DotProduct(FVector(Offset.X, Offset.Y, 0.0f), Heading))
			/ AChaosImpactTornado::TravelSpeed, 0.0f, LookAhead);
		const FVector Future = Tornado->GetCenter() + Heading * AChaosImpactTornado::TravelSpeed * Ahead;
		// Its weave swings it about a funnel's width either side of its line.
		const float Reach = AChaosImpactTornado::CatchRadius + CapsuleRadius + 110.0f;
		const float Miss = static_cast<float>(FVector::Dist2D(Location, Future));
		if (Miss < Reach && Miss < ThreatMiss)
		{
			Threat = Tornado;
			ThreatMiss = Miss;
			ThreatDistance = static_cast<float>(FVector::Dist2D(Location, Tornado->GetCenter()));
		}
	}
	if (!Threat)
	{
		return Now < TornadoEvadeUntil && Now < EvadeUntil;
	}

	// Off its line to the side already nearer, into open floor, never back into its way.
	const FVector Heading = Threat->GetTravelDirection();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Heading);
	const float Sign = FVector::DotProduct(Location - Threat->GetCenter(), Across) >= 0.0 ? 1.0f : -1.0f;
	FVector Best = Across * Sign;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 Candidate = 0; Candidate < 16; ++Candidate)
	{
		const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 22.5f, FVector::UpVector);
		const float Free = GetFreeTravel(Location, Direction, 500.0f);
		const float Score = Free + 260.0f * FVector::DotProduct(Direction, Across * Sign)
			- 320.0f * FMath::Max(0.0f, static_cast<float>(FVector::DotProduct(Direction, Heading)))
			- (Free < 150.0f ? 400.0f : 0.0f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Direction;
		}
	}
	EvadeDirection = Best;
	EvadeUntil = Now + 0.4f;
	TornadoEvadeUntil = EvadeUntil;
	// Too close to walk clear: dash out.
	if (Skill > 0.25f && ThreatDistance < AChaosImpactTornado::CatchRadius + CapsuleRadius + 90.0f && Self->CanDashNow()
		&& GetFreeTravel(Location, Best, 600.0f) > Self->GetDashDistance() * 0.6f)
	{
		Self->RequestAIDash(Best);
	}
	return true;
}

bool AChaosImpactCPUController::UpdateEvasion(AChaosImpactCharacter* Self, const float Now)
{
	if (Self->IsDashing())
	{
		return Now < EvadeUntil;
	}
	TArray<FBallThreat> Threats;
	CollectThreats(Self, Now, Threats);
	if (bPerfect)
	{
		CollectActiveBurns(Self);
	}
	if (Threats.IsEmpty()
		|| EvaluateEscape(Self, Threats, FVector::ZeroVector, 0.0f, false, false) >= GetSafeMargin())
	{
		return Now < EvadeUntil;
	}
	if (Self->IsChargingNova())
	{
		// Rooted by a nova's charge with something coming: call it off to get out of the way.
		Self->CancelChargingThrow();
	}
	const int32 WalkCandidates = bPerfect ? 32 : 16;
	const int32 DashCandidates = bPerfect ? 24 : 12;

	const FVector Location = Self->GetActorLocation();
	// Keep a committed dodge while it still works, so the CPU does not dither between sides.
	if (Now < EvadeUntil && !EvadeDirection.IsNearlyZero()
		&& EvaluateEscape(Self, Threats, EvadeDirection,
			GetFreeTravel(Location, EvadeDirection, 700.0f), false, false) >= GetSafeMargin())
	{
		return true;
	}

	const float Contact = EarliestContactSeconds(Self, Threats);
	FVector BestWalk = FVector::ZeroVector;
	float BestWalkClearance = -TNumericLimits<float>::Max();
	float BestWalkScore = -TNumericLimits<float>::Max();
	for (int32 Candidate = 0; Candidate < WalkCandidates; ++Candidate)
	{
		const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 360.0f / WalkCandidates, FVector::UpVector);
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
	if (BestWalkClearance >= GetSafeMargin() * 0.5f)
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
		for (int32 Candidate = 0; Candidate < DashCandidates; ++Candidate)
		{
			const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 360.0f / DashCandidates, FVector::UpVector);
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
		// さいきょう dashes at the last moment, so the dash's untouchable instant is spent on the hit itself and the
		// stamina only where nothing else would do.
		const float DashAt = bPerfect ? Self->GetDashDuration() + 0.06f + LearnedDashLead : FMath::Lerp(0.2f, 0.3f, Skill);
		if (!BestDash.IsNearlyZero() && BestDashClearance > BestWalkClearance && Contact <= DashAt)
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
		if (Candidate == Self || Candidate->IsEliminated() || AChaosImpactGameState::AreTeammates(GetWorld(), Candidate, Self))
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
		if (bPerfect)
		{
			// Whoever cannot dash away right now (or cannot move at all) is the one to hit.
			Score += Candidate->GetDashReadyInSeconds() > 0.5f ? 0.9f : 0.0f;
			Score += Candidate->IsIceFrozen() || Candidate->IsChargingNova() ? 1.5f : 0.0f;
			// Whoever is ahead on points is the one to bring down (a lead is what lets a player hide).
			Score += FMath::Clamp(GetPoints(Candidate) - GetPoints(Self), 0, 4) * 0.9f;
		}
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

	// Weaker CPUs lead a moving target only partly (LeadScale), so a runner often gets away.
	FVector TargetVelocity = GetObservedVelocity(Target) * LeadScale;
	TargetVelocity.Z = 0.0f;
	TargetVelocity = TargetVelocity.GetClampedToMaxSize(1200.0f);
	const FVector TargetLocation = Target->GetActorLocation();

	// Solve for the interception point: the target keeps moving during release and flight, and a player who
	// is turning or picking up speed is still changing that motion while the ball is in the air.
	// Only a target that is already running gets the acceleration term: on a standing player the smoothed
	// value is just noise, and leading by it throws the shot wide of someone who never moved.
	const FVector TargetAcceleration = TargetVelocity.Size2D() > 150.0f
		? (GetObservedAcceleration(Target) * LeadScale).GetClampedToMaxSize(2000.0f) : FVector::ZeroVector;
	FVector Predicted = TargetLocation;
	float Flight = FVector::Dist2D(Origin, Predicted) / Speed;
	for (int32 Iteration = 0; Iteration < 5; ++Iteration)
	{
		const float Ahead = Delay + Flight;
		const FVector Curve = (TargetAcceleration * (0.5f * Ahead * Ahead)).GetClampedToMaxSize(140.0f);
		Predicted = TargetLocation + TargetVelocity * Ahead + Curve;
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
	else
	{
		// The straight-line solution can miss by a hair once the simulated flight bends or clips a corner.
		// Sweep a narrow fan around it rather than giving up and walking closer.
		static constexpr float TrimDegrees[] = {1.5f, -1.5f, 3.0f, -3.0f, 5.0f, -5.0f, 8.0f, -8.0f};
		for (const float Trim : TrimDegrees)
		{
			const FVector Trimmed = Direction.RotateAngleAxis(Trim, FVector::UpVector);
			SimulateBallPath(Origin, Trimmed * Speed, bArc, Horizon, ShotStepSeconds, Self, Target, Path);
			const float TrimmedArrival = FindShotArrival(Path, ShotStepSeconds, Target, TargetVelocity, Delay);
			if (TrimmedArrival >= 0.0f)
			{
				Arrival = TrimmedArrival;
				Plan.bValid = true;
				Plan.Direction = Trimmed;
				break;
			}
		}
	}
	if (!Plan.bValid && bAllowBank)
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
	// Never throw through a teammate: the ball would stop on them and the shot is wasted.
	SimulateBallPath(Origin, Plan.Direction * Speed, bArc, FMath::Min(Arrival + 0.05f, 1.6f),
		ShotStepSeconds, Self, Target, Path);
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const AChaosImpactCharacter* Mate = *It;
		if (Mate == Self || Mate == Target || Mate->IsEliminated()
			|| !AChaosImpactGameState::AreTeammates(GetWorld(), Mate, Self))
		{
			continue;
		}
		const float BlockRadius = Mate->GetCapsuleComponent()->GetScaledCapsuleRadius() + BallRadius;
		for (const FVector& Point : Path)
		{
			if (FVector::Dist2D(Point, Mate->GetActorLocation()) < BlockRadius
				&& FMath::Abs(Point.Z - Mate->GetActorLocation().Z)
					<= Mate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
			{
				Plan.bValid = false;
				return Plan;
			}
		}
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
	if (bPerfect)
	{
		UpdateOffensePerfect(Self, Target, Now);
		return;
	}
	const bool bBankSearch = Skill >= 0.45f && Now >= NextBankSearchAt;

	if (Self->GetCarriedBallType(0) == EChaosImpactBallType::Nova)
	{
		// A nova: charged rooted to the spot (a weak CPU lets go early), aimed straight at the target (its blast is
		// wide), and let go.
		const FVector ToTarget = Target->GetActorLocation() - Self->GetActorLocation();
		if (!Self->IsChargingThrow())
		{
			if (Now >= NextThrowAt && !Self->IsDashing() && !Self->IsThrowReleasePending() && ToTarget.Size2D() < 3200.0f)
			{
				Self->BeginThrowInput();
			}
			return;
		}
		Self->SetAIAimDirection(ShakeAim(ToTarget.GetSafeNormal2D()));
		if (Self->GetThrowChargeAlpha() >= (Skill < 0.3f ? 0.6f : 1.0f))
		{
			Self->EndThrowInput();
			NextThrowAt = Now + FMath::Lerp(1.2f, 0.6f, Skill) * ThrowDelayScale;
		}
		return;
	}

	if (!Self->IsChargingThrow())
	{
		if (Now < NextThrowAt || Self->IsDashing() || Self->IsThrowReleasePending())
		{
			return;
		}
		DesiredChargeAlpha = ChooseDesiredCharge(Self, Target);
		ShotAimErrorDegrees = FMath::FRandRange(-1.0f, 1.0f) * FMath::Lerp(7.0f, 0.15f, Skill);
		ReleaseShakeThisThrow = FMath::FRandRange(-1.0f, 1.0f) * ReleaseShakeDegrees
			// Throwing blind into the smoke.
			+ (Self->IsBlinded() ? FMath::FRandRange(-25.0f, 25.0f) : 0.0f);
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
					Self->SetAIAimDirection(ShakeAim(Preview.Direction));
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
		Self->SetAIAimDirection(ShakeAim(Plan.Direction));
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
		NextThrowAt = Now + (Balls > 1 ? FMath::Lerp(0.6f, 0.26f, Skill) : FMath::Lerp(0.9f, 0.45f, Skill)) * ThrowDelayScale;
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
		if (bPerfect)
		{
			// The better the ball, the further it is worth going; never into burning ground or a black hole. Hunting,
			// any ball near at hand and on the way to the runner beats a better one across the stage.
			Score -= (bHunting ? GetHuntPickupValue(Ball->GetBallType()) * 220.0f : GetPickupValue(Ball->GetBallType()) * 320.0f);
			Score += GetHazardAt(Self, BallLocation) * 900.0f;
			if (bHunting && CurrentTarget.IsValid())
			{
				const FVector ToTarget = (CurrentTarget->GetActorLocation() - Location).GetSafeNormal2D();
				Score -= static_cast<float>(FVector::DotProduct((BallLocation - Location).GetSafeNormal2D(), ToTarget)) * 350.0f;
			}
		}
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
			NextThrowAt = FMath::Max(NextThrowAt, Now + (bPerfect ? 0.0f : FMath::Lerp(0.45f, 0.12f, Skill) * ThrowDelayScale));
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
		|| (PickupDistance < 420.0f && (!Target || TargetDistance > 700.0f) && !Self->IsChargingThrow())
		// Hunting: a second ball near at hand is fetched first (the first throw takes their dash, the second lands).
		|| (bPerfect && bHunting && Balls == 1 && PickupDistance < 900.0f && (!Target || TargetDistance > 800.0f)));

	if (bPerfect)
	{
		// A snowball in hand and nobody close enough to punish standing still: roll it up on the spot.
		const bool bSnowToGrow = Balls > 0 && Self->GetCarriedBallType(0) == EChaosImpactBallType::Snow && Self->GetSnowGrowth(0) < 0.95f;
		bSnowMashing = bSnowToGrow && (!Target || TargetDistance > 650.0f || Target->GetCarriedBallCount() == 0)
			&& CountArmedThreats(Self) <= 1 && GetHazardAt(Self, Location) < 1.0f;
		if (bSnowMashing)
		{
			// Across the line to the target, so the waggle never walks it nearer or further.
			const FVector Toward = Target ? (Target->GetActorLocation() - Location).GetSafeNormal2D() : Self->GetActorForwardVector();
			SnowMashAxis = FVector::CrossProduct(FVector::UpVector, Toward).GetSafeNormal2D();
			if (!IsPathClear(Location, SnowMashAxis, 120.0f) || !IsPathClear(Location, -SnowMashAxis, 120.0f))
			{
				SnowMashAxis = Toward;
			}
		}
	}
	if (bGoForPickup)
	{
		const float LeadSeconds = FMath::Clamp(PickupDistance / 1800.0f, 0.05f, 0.28f);
		const FVector PredictedPickup = Pickup->GetActorLocation() + Pickup->GetBallVelocity() * LeadSeconds;
		Desired = bPerfect ? RouteToward(Self, PredictedPickup, Now) : (PredictedPickup - Location).GetSafeNormal2D();
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
		if (bPerfect)
		{
			// Armed: in close, where a throw arrives before anyone can walk out of it (their throws are read in the
			// hand, so near is safe enough), a little further off when several could throw at once; holding a
			// nova, far enough off that nobody can punish its charge. Unarmed with them armed: out of reach.
			const int32 Threatening = CountArmedThreats(Self);
			Ideal = Balls == 0 ? (bTargetArmed ? 1300.0f : 900.0f)
				: Self->GetCarriedBallType(0) == EChaosImpactBallType::Nova ? 1900.0f
				: Threatening >= 3 ? 950.0f : Threatening >= 2 ? 700.0f : 360.0f;
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
		if (bPerfect && UpdateHunting(Self, Target, Now))
		{
			// Hunting: to where they would run, on the open side of them, so every way out leads to a wall. Close
			// in at full speed (no strafing), and with a dash when far (one dash always kept back for dodging).
			const FVector TargetAt = Target->GetActorLocation();
			FVector TargetVelocity = GetObservedVelocity(Target);
			TargetVelocity.Z = 0.0f;
			const FVector Escape = TargetVelocity.Size2D() > 150.0f ? TargetVelocity.GetSafeNormal2D() : (TargetAt - Location).GetSafeNormal2D();
			const AChaosImpactGameState* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
			const FVector Center = Match && Match->bVersusMatch ? FVector(Match->StageCenter) : HomeLocation;
			const FVector ToCenter = (Center - TargetAt).GetSafeNormal2D();
			const float Lead = FMath::Clamp(TargetDistance * 0.5f, 150.0f, 700.0f);
			const FVector CutOff = TargetAt + Escape * Lead
				+ ToCenter * FMath::Min(350.0f, static_cast<float>(FVector::Dist2D(Center, TargetAt)));
			if (TargetDistance > 550.0f)
			{
				Desired = RouteToward(Self, CutOff, Now);
				Scale = 1.0f;
				if (TargetDistance > 1200.0f && Now >= NextHuntDashAt && Self->CanDashNow() && !Self->IsChargingNova()
					&& Self->GetStamina() >= HuntDashStaminaReserve && CountArmedThreats(Self) == 0
					&& GetFreeTravel(Location, Desired, 700.0f) > Self->GetDashDistance() * 0.9f
					&& GetHazardAt(Self, Location + Desired * Self->GetDashDistance()) < 1.0f)
				{
					Self->RequestAIDash(Desired);
					NextHuntDashAt = Now + 1.4f;
				}
			}
		}
		if (bPerfect && Balls > 0)
		{
			// A wall between: not shuffling in front of it, but straight to somewhere with a clear shot.
			if (!HasClearShotFrom(Location, Self, Target))
			{
				const bool bReached = !VantagePoint.IsZero() && FVector::Dist2D(Location, VantagePoint) < 90.0f;
				if (VantagePoint.IsZero() || Now >= VantageUntil || bReached || !HasClearShotFrom(VantagePoint, Self, Target))
				{
					FVector Found;
					VantagePoint = FindVantagePoint(Self, Target, Ideal, Found) ? Found : FVector::ZeroVector;
					VantageUntil = Now + 1.2f;
				}
				if (!VantagePoint.IsZero())
				{
					Desired = RouteToward(Self, VantagePoint, Now);
					Scale = 1.0f;
				}
			}
			else
			{
				VantagePoint = FVector::ZeroVector;
			}
		}
	}
	else if (FVector::Dist2D(Location, HomeLocation) > 250.0f)
	{
		Desired = (HomeLocation - Location).GetSafeNormal2D();
		Scale = 0.6f;
	}

	// Step out of a crowd before anything else; two characters pushing into each other go nowhere.
	const FVector Separation = GetSeparationDirection(Self);
	if (!Separation.IsNearlyZero())
	{
		Desired = (Desired + Separation * 0.75f).GetSafeNormal2D();
		Scale = FMath::Max(Scale, 0.6f);
	}

	if (bPerfect)
	{
		// Never stand in (or walk into) burning ground, a black hole, smoke, a tornado's way, a nova's landing or
		// off the stage's edge; deep in one, dash out if a dash can be spared.
		const float Here = GetHazardAt(Self, Location);
		Desired = ChooseSafeDirection(Self, Desired);
		if (Here >= 1.0f)
		{
			Scale = 1.0f;
		}
		if (Here >= 3.0f && Self->CanDashNow() && Self->GetStamina() >= 1.0f && !Desired.IsNearlyZero()
			&& GetFreeTravel(Location, Desired, 400.0f) > Self->GetDashDistance() * 0.8f)
		{
			Self->RequestAIDash(Desired);
		}
	}
	if (Desired.IsNearlyZero())
	{
		DesiredMoveDirection = FVector::ZeroVector;
		DesiredMoveScale = 0.0f;
		return;
	}
	const FVector AvoidedDirection = SteerAroundObstacles(Location, Desired, Now);
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
	constexpr float LookAhead = 430.0f;
	FVector WallNormal;
	const float ForwardFree = SweepWalls(Origin, Forward, LookAhead, WallNormal);
	if (ForwardFree >= LookAhead)
	{
		return Forward;
	}

	// Slide along the wall that is in the way: at a shallow angle this keeps the CPU moving past it
	// instead of stopping dead and picking a whole new direction every decision.
	if (!WallNormal.IsNearlyZero())
	{
		const FVector Slide = (Forward - WallNormal * FVector::DotProduct(Forward, WallNormal)).GetSafeNormal2D();
		if (!Slide.IsNearlyZero() && FVector::DotProduct(Slide, Forward) > 0.15f
			&& GetFreeTravel(Origin, Slide, 260.0f) >= 250.0f)
		{
			return Slide;
		}
	}

	static constexpr float CandidateAngles[] =
	{
		32.0f, -32.0f, 58.0f, -58.0f, 88.0f, -88.0f, 125.0f, -125.0f, 180.0f
	};
	FVector BestDirection = -Forward;
	float BestScore = -TNumericLimits<float>::Max();
	for (const float Angle : CandidateAngles)
	{
		const FVector Candidate = Forward.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal2D();
		const float Clearance = GetFreeTravel(Origin, Candidate, LookAhead) / LookAhead;
		const float Alignment = FVector::DotProduct(Candidate, Forward);
		const float PreferredSide = FMath::Sign(Angle) == FMath::Sign(StrafeSign) ? 0.12f : 0.0f;
		// Keeping the way already taken beats a mirror-image detour that is only marginally better.
		const float Continuity = AvoidCommitDirection.IsNearlyZero()
			? 0.0f : FVector::DotProduct(Candidate, AvoidCommitDirection) * 0.35f;
		const float Score = Clearance * 3.2f + Alignment * 1.65f + PreferredSide + Continuity;
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

float AChaosImpactCPUController::SweepWalls(const FVector& Origin, const FVector& Direction,
	const float MaxDistance, FVector& OutWallNormal, const AActor* IgnoredActor) const
{
	OutWallNormal = FVector::ZeroVector;
	if (!GetWorld() || Direction.IsNearlyZero())
	{
		return 0.0f;
	}
	// Only the level's own geometry counts as an obstacle. Balls are world-dynamic and other characters are
	// pawns: treating either as a wall is what used to leave the CPU shuffling against nothing.
	FCollisionObjectQueryParams WallObjects;
	WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUClearance), false, GetPawn());
	Parameters.AddIgnoredActor(IgnoredActor);
	FHitResult Hit;
	const FVector Start = Origin + FVector::UpVector * 68.0f;
	const FVector Unit = Direction.GetSafeNormal2D();
	if (!GetWorld()->SweepSingleByObjectType(Hit, Start, Start + Unit * MaxDistance, FQuat::Identity,
		WallObjects, FCollisionShape::MakeCapsule(38.0f, 62.0f), Parameters))
	{
		return MaxDistance;
	}
	OutWallNormal = Hit.ImpactNormal.GetSafeNormal2D();
	return Hit.Time * MaxDistance;
}

float AChaosImpactCPUController::GetFreeTravel(const FVector& Origin, const FVector& Direction,
	const float MaxDistance, const AActor* IgnoredActor) const
{
	FVector WallNormal;
	return SweepWalls(Origin, Direction, MaxDistance, WallNormal, IgnoredActor);
}

FVector AChaosImpactCPUController::GetSeparationDirection(const AChaosImpactCharacter* Self) const
{
	if (!Self || !GetWorld())
	{
		return FVector::ZeroVector;
	}
	constexpr float PersonalSpace = 170.0f;
	const FVector Location = Self->GetActorLocation();
	FVector Away = FVector::ZeroVector;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const AChaosImpactCharacter* Other = *It;
		if (Other == Self || Other->IsEliminated())
		{
			continue;
		}
		const FVector Offset = Location - Other->GetActorLocation();
		const float Distance = Offset.Size2D();
		if (Distance > KINDA_SMALL_NUMBER && Distance < PersonalSpace)
		{
			Away += Offset.GetSafeNormal2D() * (1.0f - Distance / PersonalSpace);
		}
	}
	return Away.GetClampedToMaxSize(1.0f);
}

FVector AChaosImpactCPUController::SteerAroundObstacles(const FVector& Origin, const FVector& DesiredDirection,
	const float Now)
{
	const FVector Forward = DesiredDirection.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return Forward;
	}
	// The way already chosen is kept while it still leads somewhere; changing it every decision is what
	// made the CPU rock left and right in a doorway instead of going through it.
	if (Now < AvoidCommitUntil && !AvoidCommitDirection.IsNearlyZero()
		&& FVector::DotProduct(AvoidCommitDirection, Forward) > -0.75f
		&& IsPathClear(Origin, AvoidCommitDirection, 220.0f))
	{
		return AvoidCommitDirection;
	}
	const FVector Steered = AvoidNearbyObstacle(Origin, Forward);
	if (!Steered.Equals(Forward, 0.02f))
	{
		AvoidCommitDirection = Steered;
		AvoidCommitUntil = Now + 0.45f;
	}
	else
	{
		AvoidCommitUntil = 0.0f;
	}
	return Steered;
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
	if (!ControlledCharacter || Now - LastProgressCheckAt < 0.4f)
	{
		return;
	}
	const FVector Location = ControlledCharacter->GetActorLocation();
	const float Progress = FVector::Dist2D(Location, LastProgressLocation);
	const bool bWasTryingToMove = DesiredMoveScale > 0.25f && Now >= EvadeUntil
		&& !DesiredMoveDirection.IsNearlyZero() && !ControlledCharacter->IsDashing();
	// Expected travel for the time that passed; anything far below it means something is in the way.
	const float Expected = ControlledCharacter->GetCharacterMovement()->MaxWalkSpeed
		* (Now - LastProgressCheckAt) * DesiredMoveScale;
	if (bWasTryingToMove && Progress < FMath::Max(34.0f, Expected * 0.35f))
	{
		++StuckStreak;
		// Each try gets bolder: a sidestep, then the other way and a wider angle, then straight back out.
		const float Angle = StuckStreak == 1 ? 92.0f : StuckStreak == 2 ? 132.0f : 180.0f;
		StrafeSign *= -1.0f;
		const FVector SideStep = DesiredMoveDirection.RotateAngleAxis(
			StrafeSign * Angle, FVector::UpVector).GetSafeNormal2D();
		AvoidCommitUntil = 0.0f;
		EscapeMoveDirection = AvoidNearbyObstacle(Location, SideStep);
		if (EscapeMoveDirection.IsNearlyZero())
		{
			EscapeMoveDirection = -DesiredMoveDirection;
		}
		AvoidCommitDirection = EscapeMoveDirection;
		AvoidCommitUntil = Now + 0.6f;
		EscapeUntil = Now + 0.85f;
		TryTraverseObstacle(ControlledCharacter, EscapeMoveDirection, Now);
		// Still wedged after three tries: a dash breaks free of geometry a walk cannot leave.
		if (StuckStreak >= 3 && ControlledCharacter->CanDashNow() && (!bPerfect || ControlledCharacter->GetStamina() >= 3.0f)
			&& GetFreeTravel(Location, EscapeMoveDirection, 420.0f) > ControlledCharacter->GetDashDistance())
		{
			ControlledCharacter->RequestAIDash(EscapeMoveDirection);
			StuckStreak = 0;
		}
	}
	else if (Progress > 60.0f)
	{
		StuckStreak = 0;
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
	FCollisionObjectQueryParams WallObjects;
	WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUJump), false, GetPawn());
	FHitResult LowHit;
	FHitResult HighHit;
	const FVector LowStart = Origin + FVector::UpVector * 32.0f;
	const FVector HighStart = Origin + FVector::UpVector * 125.0f;
	const FVector Offset = DesiredDirection * 150.0f;
	// Only real ledges are jumped: a ball rolling past used to read as a step and set the CPU hopping.
	const bool bLowBlocked = GetWorld()->LineTraceSingleByObjectType(
		LowHit, LowStart, LowStart + Offset, WallObjects, Parameters);
	const bool bHighBlocked = GetWorld()->LineTraceSingleByObjectType(
		HighHit, HighStart, HighStart + Offset, WallObjects, Parameters);
	return bLowBlocked && !bHighBlocked;
}

// ---------------------------------------------------------------------------------------------
// さいきょう
//
// Plays the game out exactly. It sees every throw the moment it leaves the hand and flies it as it really goes (its own
// size, gravity, homing, rebounds and speed-ups, and where a special ball bursts and how far its blast reaches), so it
// dodges everything a walk, a jump or a last-instant dash can get out of, keeping its stamina for that alone. It always
// holds a fully charged throw and lets it go only once the target can neither walk out of it nor dash in time: when
// they are frozen, rooted, out of stamina or still in the cooldown of a dash - and with two balls in hand it spends the
// first to make them dash and the second in that cooldown. It keeps out of burning ground, black holes, smoke,
// tornadoes, a nova's landing and the stage's edge, takes the best balls first, and charges a nova only where nobody can
// punish it.

namespace
{
	/** A human's quickest reaction to a throw, when judging whether a shot can be escaped. */
	constexpr float SureShotReactionSeconds = 0.15f;
}

float AChaosImpactCPUController::GetBurstRadius(const EChaosImpactBallType Type, const float Scale)
{
	switch (Type)
	{
	case EChaosImpactBallType::Fire: return AChaosImpactHazardZone::FireRadius;
	case EChaosImpactBallType::Ice: return AChaosImpactHazardZone::IceRadius;
	case EChaosImpactBallType::Smoke: return ChaosImpactBallTypes::SmokeRadius;
	// Its pull is faster than walking: anywhere well inside it ends at its burning centre.
	case EChaosImpactBallType::Black: return AChaosImpactHazardZone::BlackHoleRadius * 0.8f;
	case EChaosImpactBallType::Nova: return ChaosImpactBallTypes::GetNovaBlastRadius(Scale);
	case EChaosImpactBallType::Simae: return 0.0f;
	default: return 0.0f;
	}
}

float AChaosImpactCPUController::GetHuntPickupValue(const EChaosImpactBallType Type)
{
	// Against a runner: what arrives before they can step aside (a beam, a thunder ball), what covers the ground they
	// would step to (a big snowball, a nova, a black hole, ice), and a drive ball that follows them.
	switch (Type)
	{
	case EChaosImpactBallType::Beam: return 3.5f;
	case EChaosImpactBallType::Thunder: return 3.0f;
	case EChaosImpactBallType::Drive: return 2.8f;
	case EChaosImpactBallType::Ice: return 2.4f;
	case EChaosImpactBallType::Snow: return 2.3f;
	case EChaosImpactBallType::Black: return 2.2f;
	case EChaosImpactBallType::Nova: return 2.0f;
	case EChaosImpactBallType::Fire: return 1.8f;
	default: return 1.0f;
	}
}

float AChaosImpactCPUController::GetPickupValue(const EChaosImpactBallType Type)
{
	switch (Type)
	{
	case EChaosImpactBallType::Beam: return 3.0f;
	case EChaosImpactBallType::Smoke: return 2.8f;
	case EChaosImpactBallType::Ice: return 2.6f;
	case EChaosImpactBallType::Fire: return 2.5f;
	case EChaosImpactBallType::Thunder: return 2.2f;
	case EChaosImpactBallType::Black: return 2.0f;
	case EChaosImpactBallType::Nova: return 1.8f;
	case EChaosImpactBallType::Simae: return 1.7f;
	case EChaosImpactBallType::Drive: return 1.9f;
	case EChaosImpactBallType::Wind: return 1.6f;
	case EChaosImpactBallType::Snow: return 1.5f;
	default: return 1.0f;
	}
}

void AChaosImpactCPUController::SimulateTypedFlight(const FVector& Start, FVector Velocity, const EChaosImpactBallType Type,
	const float Radius, const bool bArc, const float GravityScale, const float HorizonSeconds, const float StepSeconds,
	const AActor* IgnoredA, const AActor* IgnoredB, const AChaosImpactCharacter* HomingTarget, FBallThreat& Out) const
{
	Out.Path.Reset();
	Out.Path.Add(Start);
	Out.StepSeconds = StepSeconds;
	Out.HitRadius = Radius;
	Out.HitHeight = 0.0f;
	Out.BurstRadius = 0.0f;
	Out.bBursts = false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	const bool bBeam = Type == EChaosImpactBallType::Beam;
	const bool bThunder = Type == EChaosImpactBallType::Thunder;
	// Special balls burst on their first contact with anything (a thunder ball rebounds; a beam goes through).
	const bool bBurstOnContact = Type != EChaosImpactBallType::Normal && !bThunder && !bBeam;
	if (bBeam)
	{
		Out.HitRadius = BeamHitRadius;
		Out.HitHeight = BeamHitHeight;
	}
	Out.bTrail = Type == EChaosImpactBallType::Fire;
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUTypedFlight), false, GetPawn());
	Parameters.AddIgnoredActor(IgnoredA);
	Parameters.AddIgnoredActor(IgnoredB);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(FMath::Max(Radius, 4.0f));
	const float GravityZ = World->GetGravityZ() * GravityScale;
	FVector Position = Start;
	float Travelled = 0.0f;
	const int32 Steps = FMath::CeilToInt(HorizonSeconds / StepSeconds);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		if (HomingTarget && Type == EChaosImpactBallType::Normal)
		{
			// A normal ball bends toward the one ahead of it, a little each moment.
			const FVector Flat(Velocity.X, Velocity.Y, 0.0f);
			const FVector To = HomingTarget->GetActorLocation() - Position;
			const FVector ToFlat(To.X, To.Y, 0.0f);
			if (ToFlat.Size() < NormalHomingReach && Flat.Size() > 1.0f)
			{
				const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					static_cast<float>(FVector::DotProduct(Flat.GetSafeNormal(), ToFlat.GetSafeNormal())), -1.0f, 1.0f)));
				if (Angle < NormalHomingConeDegrees && Angle > 0.01f)
				{
					const float Turn = FMath::Min(Angle, NormalHomingDegreesPerSecond * StepSeconds);
					const float Sign = FVector::CrossProduct(Flat, ToFlat).Z >= 0.0 ? 1.0f : -1.0f;
					const FVector Bent = Flat.RotateAngleAxis(Turn * Sign, FVector::UpVector);
					Velocity.X = Bent.X;
					Velocity.Y = Bent.Y;
				}
			}
		}
		FVector NextVelocity = Velocity;
		if (bArc)
		{
			NextVelocity.Z += GravityZ * StepSeconds;
		}
		const FVector Next = Position + (Velocity + NextVelocity) * 0.5f * StepSeconds;
		if (bBeam)
		{
			// Light passes through everything, to the end of its range.
			Travelled += static_cast<float>(FVector::Dist(Position, Next));
			Position = Next;
			Out.Path.Add(Position);
			if (Travelled >= BeamRange)
			{
				return;
			}
			continue;
		}
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Position, Next, FQuat::Identity, ECC_Visibility, Sphere, Parameters)
			&& !Hit.bStartPenetrating)
		{
			if (bBurstOnContact)
			{
				Out.Path.Add(Hit.Location);
				Out.bBursts = true;
				Out.BurstRadius = GetBurstRadius(Type, Radius / 24.0f);
				return;
			}
			if (bArc && Hit.ImpactNormal.Z > 0.65f)
			{
				// Lands and lies there, harmless.
				Out.Path.Add(Hit.Location);
				return;
			}
			FVector Normal = Hit.ImpactNormal;
			if (!bArc)
			{
				Normal.Z = 0.0f;
			}
			Normal = Normal.GetSafeNormal();
			Position = Hit.Location;
			NextVelocity = (NextVelocity - 2.0f * FVector::DotProduct(NextVelocity, Normal) * Normal) * (bArc ? 0.72f : 1.0f);
			if (!bArc)
			{
				NextVelocity.Z = 0.0f;
			}
			if (bThunder)
			{
				// Faster off every wall.
				NextVelocity = NextVelocity.GetSafeNormal() * FMath::Min(static_cast<float>(NextVelocity.Size()) * ThunderBounceSpeedUp, ThunderMaxSpeed);
			}
		}
		else
		{
			Position = Next;
		}
		Velocity = NextVelocity;
		Out.Path.Add(Position);
	}
}

void AChaosImpactCPUController::CollectExactThreats(const AChaosImpactCharacter* Self, TArray<FBallThreat>& OutThreats) const
{
	UWorld* World = GetWorld();
	const FVector Location = Self->GetActorLocation();
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
	{
		const AChaosImpactBall* Ball = *It;
		if (Ball->IsPickup() || Ball->WasThrownBy(Self) || Ball->HasDetonated() || Ball->GetAttachParentActor()
			|| AChaosImpactGameState::AreTeammates(World, Ball->GetThrowingPawn(), Self)
			|| UnnoticedBalls.Contains(Ball))
		{
			continue;
		}
		const FVector Velocity = Ball->GetBallVelocity();
		if (Velocity.SizeSquared() < FMath::Square(50.0f))
		{
			continue;
		}
		FBallThreat Threat;
		SimulateTypedFlight(Ball->GetActorLocation(), Velocity, Ball->GetBallType(), Ball->GetHitRadius(), Ball->IsArcFlight(),
			Ball->GetFlightGravityScale(), 1.6f + LearnedHorizonBonus, ThreatStepSeconds, Ball, nullptr, Self, Threat);
		if (Threat.bBursts && Threat.BurstRadius > 0.0f)
		{
			Threat.BurstRadius += LearnedBurstMargin;
		}
		float Closest = TNumericLimits<float>::Max();
		for (const FVector& Point : Threat.Path)
		{
			Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Point, Location)) - Threat.HitRadius);
		}
		if (Threat.bBursts && Threat.Path.Num() > 0)
		{
			Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Threat.Path.Last(), Location)) - Threat.BurstRadius);
		}
		if (Closest < 420.0f)
		{
			OutThreats.Add(MoveTemp(Threat));
		}
	}
	// A throw already let go of but still in the hand: which way it goes and how fast is settled, so its flight is
	// known a moment before it leaves the hand.
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		const AChaosImpactCharacter* Other = *It;
		AChaosImpactBall* Ball = nullptr;
		FVector Direction;
		float Speed = 0.0f;
		float Upward = 0.0f;
		bool bArc = true;
		float SecondsLeft = 0.0f;
		if (Other == Self || Other->IsEliminated() || AChaosImpactGameState::AreTeammates(World, Other, Self)
			|| !Other->GetPendingThrow(Ball, Direction, Speed, Upward, bArc, SecondsLeft) || !Ball)
		{
			continue;
		}
		const EChaosImpactBallType Type = Ball->GetBallType();
		if (Type == EChaosImpactBallType::Wind)
		{
			// A tornado, not a flight: seen to by the tornado dodge once it is out.
			continue;
		}
		if (Type == EChaosImpactBallType::Thunder || Type == EChaosImpactBallType::Beam)
		{
			Speed = Type == EChaosImpactBallType::Thunder ? ChaosImpactBallTypes::ThunderSpeed : ChaosImpactBallTypes::BeamSpeed;
			bArc = false;
			Upward = 0.0f;
		}
		const FVector Start = Ball->GetActorLocation();
		FBallThreat Threat;
		SimulateTypedFlight(Start, Direction.GetSafeNormal2D() * Speed + FVector(0.0f, 0.0f, bArc ? Upward : 0.0f), Type,
			Ball->GetHitRadius(), bArc, Type == EChaosImpactBallType::Nova ? ChaosImpactBallTypes::NovaGravityScale : 1.0f,
			1.6f + LearnedHorizonBonus, ThreatStepSeconds, Ball, Other, Self, Threat);
		if (Threat.bBursts && Threat.BurstRadius > 0.0f)
		{
			Threat.BurstRadius += LearnedBurstMargin;
		}
		// Held in the hand until it goes.
		const int32 Hold = FMath::Clamp(FMath::RoundToInt(SecondsLeft / ThreatStepSeconds), 0, 20);
		for (int32 Step = 0; Step < Hold; ++Step)
		{
			Threat.Path.Insert(Start, 0);
		}
		float Closest = TNumericLimits<float>::Max();
		for (const FVector& Point : Threat.Path)
		{
			Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Point, Location)) - Threat.HitRadius);
		}
		if (Threat.bBursts && Threat.Path.Num() > 0)
		{
			Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Threat.Path.Last(), Location)) - Threat.BurstRadius);
		}
		if (Closest < 420.0f)
		{
			OutThreats.Add(MoveTemp(Threat));
		}
	}
}

FVector AChaosImpactCPUController::PredictTargetAt(const AChaosImpactCharacter* Target, const float Seconds) const
{
	FVector Where = Target->GetActorLocation();
	if (Target->IsIceFrozen() || Target->IsChargingNova() || Seconds <= 0.0f)
	{
		return Where;
	}
	FVector Velocity = Target->IsDashing() ? GetObservedVelocity(Target) : Target->GetVelocity();
	Velocity.Z = 0.0f;
	float Moving = Seconds;
	if (Target->IsDashing())
	{
		// A dash carries it to its end and no further.
		Moving = FMath::Min(Seconds, Target->GetDashRemainingSeconds());
	}
	FVector Motion = Velocity * Moving;
	// A black hole draws it toward its centre.
	for (TActorIterator<AChaosImpactHazardZone> It(GetWorld()); It; ++It)
	{
		const AChaosImpactHazardZone* Zone = *It;
		if (Zone->GetZoneType() != EChaosImpactBallType::Black || Zone->GetAge() > Zone->GetActiveSeconds()
			|| Zone->GetSourcePawn() == Target)
		{
			continue;
		}
		const FVector ToCentre = Zone->GetActorLocation() - Where;
		const float Distance = static_cast<float>(ToCentre.Size2D());
		if (Distance < AChaosImpactHazardZone::BlackHoleRadius && Distance > 40.0f)
		{
			Motion += ToCentre.GetSafeNormal2D() * FMath::Min(Distance - 40.0f, AChaosImpactHazardZone::BlackHolePullSpeed * Seconds);
		}
	}
	// Not through walls.
	const float Length = static_cast<float>(Motion.Size2D());
	if (Length > 1.0f)
	{
		Motion = Motion.GetSafeNormal2D() * FMath::Min(Length, GetFreeTravel(Where, Motion, Length, Target));
	}
	return Where + Motion;
}

AChaosImpactCPUController::FSureShot AChaosImpactCPUController::PlanSureShot(
	const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, const float ChargeAlpha) const
{
	FSureShot Best;
	UWorld* World = GetWorld();
	if (!Self || !Target || !World || Target->IsCarriedByWind())
	{
		return Best;
	}
	using namespace ChaosImpactBallTypes;
	const EChaosImpactBallType Type = Self->GetCarriedBallType(0);
	const float Scale = Type == EChaosImpactBallType::Snow ? GetSnowScale(Self->GetSnowGrowth(0)) : 1.0f;
	float Horizontal = 0.0f;
	float Upward = 0.0f;
	EChaosImpactBallFlightMode Mode = EChaosImpactBallFlightMode::Arc;
	bool bOverhead = false;
	Self->GetThrowFlight(ChargeAlpha, Type, Scale, Horizontal, Upward, Mode, bOverhead);
	bool bArc = Mode == EChaosImpactBallFlightMode::Arc;
	float Speed = Horizontal;
	if (Type == EChaosImpactBallType::Thunder || Type == EChaosImpactBallType::Beam)
	{
		// These fly straight at their own speed, however the throw was charged.
		Speed = Type == EChaosImpactBallType::Thunder ? ThunderSpeed : BeamSpeed;
		bArc = false;
		Upward = 0.0f;
	}
	const float Delay = Self->GetThrowReleaseDelay();
	const float Radius = 24.0f * Scale;
	const FVector SelfLocation = Self->GetActorLocation();
	const float SelfFeet = SelfLocation.Z - Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const auto OriginFor = [&](const FVector& Aim)
	{
		return bOverhead ? SelfLocation + Self->GetOverheadHoldOffset(Type, Scale)
			: FVector(SelfLocation.X + Aim.X * 90.0f, SelfLocation.Y + Aim.Y * 90.0f, SelfFeet + LearnedReleaseHeight);
	};

	// Where to throw so the ball and the target meet.
	FVector Aim = (Target->GetActorLocation() - SelfLocation).GetSafeNormal2D();
	float Flight = static_cast<float>(FVector::Dist2D(SelfLocation, Target->GetActorLocation())) / FMath::Max(Speed, 1.0f);
	for (int32 Iteration = 0; Iteration < 6; ++Iteration)
	{
		const FVector Predicted = PredictTargetAt(Target, Delay + Flight);
		const FVector Origin = OriginFor(Aim);
		const FVector Toward = (Predicted - Origin).GetSafeNormal2D();
		if (!Toward.IsNearlyZero())
		{
			Aim = Toward;
		}
		Flight = static_cast<float>(FVector::Dist2D(Origin, Predicted)) / FMath::Max(Speed, 1.0f);
	}

	const UCapsuleComponent* TargetCapsule = Target->GetCapsuleComponent();
	const float TargetRadius = TargetCapsule->GetScaledCapsuleRadius();
	const float TargetHalfHeight = TargetCapsule->GetScaledCapsuleHalfHeight();
	const float Horizon = FMath::Min(Flight + 0.5f, 1.8f);
	// Can the target still get away? Walking at its speed after a reaction, or with a dash that is ready in time.
	const bool bRooted = Target->IsIceFrozen() || Target->IsChargingNova();
	const float TargetWalk = bRooted ? 0.0f : Target->GetCharacterMovement()->MaxWalkSpeed
		* (Target->GetCharacterMovement()->IsFalling() ? 0.35f : 1.0f);
	const float DashReady = Target->GetDashReadyInSeconds();
	// What this target has shown: how quickly it reacts, and how far off reading its movement tends to be.
	const FOpponentModel* Model = FindModel(Target);
	const float Reaction = Model ? FMath::Clamp(Model->ReactionSeconds * 0.9f, 0.09f, 0.3f) : SureShotReactionSeconds;
	const float Unpredictable = Model ? FMath::Min(Model->PredictionError, 160.0f) : 0.0f;

	TArray<float, TInlineAllocator<48>> Angles{0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 3.5f, -3.5f, 5.0f, -5.0f, 8.0f, -8.0f, 12.0f, -12.0f};
	if (!bArc && Type != EChaosImpactBallType::Beam)
	{
		// Straight balls can be banked off walls.
		for (float Bank = 18.0f; Bank <= 75.0f; Bank += 6.0f)
		{
			Angles.Add(Bank);
			Angles.Add(-Bank);
		}
	}
	FBallThreat Path;
	float BestScore = -TNumericLimits<float>::Max();
	for (const float Angle : Angles)
	{
		const FVector Direction = Aim.RotateAngleAxis(Angle, FVector::UpVector);
		const FVector Origin = OriginFor(Direction);
		SimulateTypedFlight(Origin, Direction * Speed + FVector(0.0f, 0.0f, bArc ? Upward : 0.0f), Type, Radius, bArc, 1.0f,
			Horizon, ShotStepSeconds, Self, Target, Type == EChaosImpactBallType::Normal ? Target : nullptr, Path);
		float HitSeconds = -1.0f;
		float Need = 0.0f;
		const float HeightReach = Path.HitHeight > 0.0f ? Path.HitHeight : TargetHalfHeight + Path.HitRadius;
		for (int32 Index = 0; Index < Path.Path.Num(); ++Index)
		{
			const float T = Index * ShotStepSeconds;
			const FVector Body = PredictTargetAt(Target, Delay + T);
			const FVector& Ball = Path.Path[Index];
			// An erratic mover gets a shot aimed closer to the middle of it.
			if (FMath::Abs(Ball.Z - Body.Z) <= HeightReach
				&& FVector::Dist2D(Ball, Body) < TargetRadius + Path.HitRadius * 0.85f - FMath::Min(Unpredictable * 0.25f, 30.0f))
			{
				HitSeconds = T;
				// To get out of its way it must step aside by a body and a ball.
				Need = TargetRadius + Path.HitRadius + 10.0f - Unpredictable * 0.3f;
				break;
			}
		}
		if (HitSeconds < 0.0f && Path.bBursts && Path.BurstRadius > 0.0f && Path.Path.Num() > 1)
		{
			const float T = (Path.Path.Num() - 1) * ShotStepSeconds;
			const FVector Body = PredictTargetAt(Target, Delay + T);
			const float FromBurst = static_cast<float>(FVector::Dist2D(Path.Path.Last(), Body));
			if (FromBurst < Path.BurstRadius + TargetRadius * 0.5f - Unpredictable * 0.3f)
			{
				HitSeconds = T;
				// Out of the whole blast.
				Need = Path.BurstRadius + TargetRadius - FromBurst + 10.0f - Unpredictable * 0.3f;
			}
		}
		if (HitSeconds < 0.0f)
		{
			continue;
		}
		// Never through a teammate.
		bool bBlocked = false;
		for (TActorIterator<AChaosImpactCharacter> It(World); It && !bBlocked; ++It)
		{
			const AChaosImpactCharacter* Mate = *It;
			if (Mate == Self || Mate == Target || Mate->IsEliminated() || !AChaosImpactGameState::AreTeammates(World, Mate, Self))
			{
				continue;
			}
			const float Block = Mate->GetCapsuleComponent()->GetScaledCapsuleRadius() + Path.HitRadius;
			for (int32 Index = 0; Index * ShotStepSeconds <= HitSeconds && Index < Path.Path.Num(); ++Index)
			{
				if (FVector::Dist2D(Path.Path[Index], Mate->GetActorLocation()) < Block)
				{
					bBlocked = true;
					break;
				}
			}
		}
		if (bBlocked)
		{
			continue;
		}
		const float Arrival = Delay + HitSeconds;
		const bool bWalkProof = TargetWalk * FMath::Max(0.0f, Arrival - Reaction) < Need;
		// A dash is untouchable while it lasts: one started before the hit gets through it.
		const bool bDashProof = FMath::Max(DashReady, Reaction * 0.8f) > Arrival - 0.02f;
		const bool bSure = bWalkProof && bDashProof;
		const float Score = (bSure ? 100.0f : 0.0f) + (bWalkProof ? 10.0f : 0.0f) - Arrival - FMath::Abs(Angle) * 0.002f;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best.bValid = true;
			Best.bSure = bSure;
			Best.bWalkProof = bWalkProof;
			Best.Direction = Direction;
			Best.ArrivalSeconds = Arrival;
		}
	}
	return Best;
}

void AChaosImpactCPUController::UpdateOffensePerfect(AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, const float Now)
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
	ShotAimErrorDegrees = 0.0f;
	ReleaseShakeThisThrow = 0.0f;
	const EChaosImpactBallType Held = Self->GetCarriedBallType(0);
	if (Held == EChaosImpactBallType::Nova)
	{
		UpdateNovaPerfect(Self, Target, Now);
		return;
	}
	if (!Self->IsChargingThrow())
	{
		// Always holding a throw ready.
		if (Now >= NextThrowAt && !Self->IsDashing() && !Self->IsThrowReleasePending())
		{
			Self->BeginThrowInput();
			ChargeStartedAt = Now;
		}
		return;
	}
	const float HeldSeconds = Now - ChargeStartedAt;
	if (Held == EChaosImpactBallType::Wind)
	{
		// A tornado weaves forward: let it go straight at them once they are near enough that it reaches them.
		const FVector Ahead = PredictTargetAt(Target, 0.6f) - Self->GetActorLocation();
		Self->SetAIAimDirection(Ahead.GetSafeNormal2D());
		if (Ahead.Size2D() < 1300.0f && LineOfSightTo(Target))
		{
			Self->EndThrowInput();
			NextThrowAt = Now + 0.05f;
		}
		return;
	}
	// Planned 20 times a second (it flies dozens of trial throws): a moment's old plan is as good as a new one.
	if (CachedShotTarget.Get() != Target || Now - CachedShotAt >= PerfectShotPlanSeconds || CachedShotAt < 0.0f)
	{
		CachedShot = PlanSureShot(Self, Target, Self->GetThrowChargeAlpha());
		CachedShotAt = Now;
		CachedShotTarget = Target;
	}
	const FSureShot Shot = CachedShot;
	if (Held == EChaosImpactBallType::Snow && Self->GetSnowGrowth(0) < 0.85f && HeldSeconds < 5.0f
		&& !(Shot.bSure && Self->GetSnowGrowth(0) >= 0.45f) && CountArmedThreats(Self) < 2)
	{
		// Still rolling the snowball up (see bSnowMashing): a big one bursts over a whole stretch of floor.
		Self->SetAIAimDirection(Shot.bValid ? Shot.Direction : (Target->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal2D());
		return;
	}
	if (!Shot.bValid)
	{
		Self->SetAIAimDirection((Target->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal2D());
		return;
	}
	Self->SetAIAimDirection(Shot.Direction);
	const bool bTargetCanDash = Target->GetDashReadyInSeconds() <= 0.05f;
	// Certain: nothing they can do gets them out of it.
	bool bRelease = Shot.bSure;
	// With a second ball ready, one they can only dash out of costs them a dash; the next comes in its cooldown.
	if (!bRelease && Balls >= 2 && Shot.bWalkProof && bTargetCanDash)
	{
		bRelease = true;
	}
	// Outnumbered, waiting for a certain shot costs more than it gains.
	if (!bRelease && Shot.bWalkProof && CountArmedThreats(Self) >= 2)
	{
		bRelease = true;
	}
	// One who has been seen not to dash at throws will not dash at this one either.
	if (const FOpponentModel* Model = FindModel(Target); !bRelease && Shot.bWalkProof && Model
		&& Model->ThrowsSeen >= 3 && Model->DashAnswerRate < 0.15f)
	{
		bRelease = true;
	}
	// Holding on forever gains nothing: after a while, the best one they can only dash out of; after longer, the best there is.
	if (!bRelease && ((HeldSeconds > 3.0f && Shot.bWalkProof) || HeldSeconds > 6.0f))
	{
		bRelease = true;
	}
	// Hunting a runner: every ball costs them a dash or turns them, so keep them coming; at once when they are up
	// against a wall with nowhere to step to.
	if (!bRelease && bHunting && HeldSeconds > 0.35f
		&& FVector::Dist2D(Target->GetActorLocation(), Self->GetActorLocation()) < HuntThrowRange)
	{
		FVector TargetVelocity = GetObservedVelocity(Target);
		TargetVelocity.Z = 0.0f;
		const FVector Escape = TargetVelocity.Size2D() > 150.0f ? TargetVelocity.GetSafeNormal2D()
			: (Target->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal2D();
		const bool bCornered = GetFreeTravel(Target->GetActorLocation(), Escape, 500.0f) < 350.0f;
		// Only a throw they cannot walk out of: it lands or costs them a dash, and dashes barely come back (one every
		// twelve seconds or so). Out of dashes, the next one of these lands. A throw they can walk out of only feeds them
		// a ball. With two in hand, now and then one anyway to keep them turning.
		bRelease = bCornered || Shot.bWalkProof || (Balls >= 2 && HeldSeconds > 1.6f);
	}
	if (bRelease)
	{
		NoteThrowAt(Target, Shot.Direction, Now);
		UE_LOG(LogTemp, Log, TEXT("PERFECTCPU %s throws %s (sure=%d walkproof=%d arrival=%.2f dashready=%.2f dist=%.0f)"), *GetName(),
			ChaosImpactBallTypes::GetInternalName(Held), Shot.bSure, Shot.bWalkProof, Shot.ArrivalSeconds,
			Target->GetDashReadyInSeconds(), FVector::Dist2D(Self->GetActorLocation(), Target->GetActorLocation()));
		Self->EndThrowInput();
		NextThrowAt = Now + 0.02f;
	}
}

FVector AChaosImpactCPUController::RouteToward(const AChaosImpactCharacter* Self, const FVector& Goal, const float Now)
{
	const FVector Location = Self->GetActorLocation();
	const FVector Straight = (Goal - Location).GetSafeNormal2D();
	const float Distance = static_cast<float>(FVector::Dist2D(Goal, Location));
	if (Distance < 60.0f || IsPathClear(Location, Straight, Distance))
	{
		return Straight;
	}
	// Recently worked out: it holds for a few moments.
	if (Now - RouteDirectionAt < 0.2f && !RouteDirection.IsNearlyZero() && FVector::Dist2D(Goal, RouteDirectionGoal) < 200.0f)
	{
		return RouteDirection;
	}
	const FChaosImpactCPUNavGrid* Grid = FindCPUNavGrid(GetWorld(), Location, Self);
	if (!Grid)
	{
		return Straight;
	}
	FIntPoint GoalCell = Grid->CellOf(Goal);
	GoalCell.X = FMath::Clamp(GoalCell.X, 0, Grid->NX - 1);
	GoalCell.Y = FMath::Clamp(GoalCell.Y, 0, Grid->NY - 1);
	static const FIntPoint Steps[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
	const auto CanStep = [Grid](const FIntPoint& From, const FIntPoint& Step)
	{
		const FIntPoint To = From + Step;
		// Diagonally only past two open sides (never through a wall's corner).
		return Grid->IsFree(To) && (Step.X == 0 || Step.Y == 0
			|| (Grid->IsFree(FIntPoint(From.X + Step.X, From.Y)) && Grid->IsFree(FIntPoint(From.X, From.Y + Step.Y))));
	};
	// How far every square is from the goal, worked out again when the goal moves or now and then.
	// (A runner's square changes all the time: worked out again once it has moved a few squares, or a moment later.)
	const bool bGoalMoved = FMath::Abs(GoalCell.X - RouteGoalCell.X) + FMath::Abs(GoalCell.Y - RouteGoalCell.Y) > 3;
	if ((bGoalMoved && Now - RouteBuiltAt > 0.1f) || Now - RouteBuiltAt > 0.4f || RouteDistances.Num() != Grid->NX * Grid->NY)
	{
		RouteGoalCell = GoalCell;
		RouteBuiltAt = Now;
		RouteDistances.Init(MAX_int32, Grid->NX * Grid->NY);
		TArray<FIntPoint> Queue;
		Queue.Reserve(Grid->NX * Grid->NY);
		// A goal inside a wall (a ball against it): from the open squares around it.
		for (int32 Ring = 0; Ring <= 2 && Queue.IsEmpty(); ++Ring)
		{
			for (int32 DY = -Ring; DY <= Ring; ++DY)
			{
				for (int32 DX = -Ring; DX <= Ring; ++DX)
				{
					const FIntPoint Cell(GoalCell.X + DX, GoalCell.Y + DY);
					if (Grid->IsFree(Cell) && RouteDistances[Grid->Index(Cell)] == MAX_int32)
					{
						RouteDistances[Grid->Index(Cell)] = 0;
						Queue.Add(Cell);
					}
				}
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FIntPoint Cell = Queue[Head];
			const int32 Next = RouteDistances[Grid->Index(Cell)] + 1;
			for (const FIntPoint& Step : Steps)
			{
				const FIntPoint To = Cell + Step;
				if (CanStep(Cell, Step) && RouteDistances[Grid->Index(To)] > Next)
				{
					RouteDistances[Grid->Index(To)] = Next;
					Queue.Add(To);
				}
			}
		}
	}
	// Downhill from here, square by square; then straight at the furthest of them that can be walked to directly.
	FIntPoint Cell = Grid->CellOf(Location);
	if (!Grid->IsInside(Cell))
	{
		return Straight;
	}
	if (!Grid->IsFree(Cell) || RouteDistances[Grid->Index(Cell)] == MAX_int32)
	{
		// Pressed against a wall: start from the best open square beside it.
		int32 Best = MAX_int32;
		FIntPoint BestCell = Cell;
		for (const FIntPoint& Step : Steps)
		{
			const FIntPoint To = Cell + Step;
			if (Grid->IsFree(To) && RouteDistances[Grid->Index(To)] < Best)
			{
				Best = RouteDistances[Grid->Index(To)];
				BestCell = To;
			}
		}
		if (Best == MAX_int32)
		{
			return Straight;
		}
		Cell = BestCell;
	}
	TArray<FIntPoint, TInlineAllocator<32>> Path;
	Path.Add(Cell);
	for (int32 Walk = 0; Walk < 30; ++Walk)
	{
		const int32 Here = RouteDistances[Grid->Index(Cell)];
		if (Here <= 0)
		{
			break;
		}
		FIntPoint Down = Cell;
		for (const FIntPoint& Step : Steps)
		{
			const FIntPoint To = Cell + Step;
			if (CanStep(Cell, Step) && RouteDistances[Grid->Index(To)] < RouteDistances[Grid->Index(Down)])
			{
				Down = To;
			}
		}
		if (Down == Cell)
		{
			break;
		}
		Cell = Down;
		Path.Add(Cell);
	}
	FVector Direction = (Grid->CenterOf(Path.Num() > 1 ? Path[1] : Path[0]) - Location).GetSafeNormal2D();
	static const int32 Tries[] = {29, 22, 16, 11, 7, 4, 2};
	for (const int32 Try : Tries)
	{
		if (Try >= Path.Num())
		{
			continue;
		}
		const FVector Waypoint = Grid->CenterOf(Path[Try]);
		const FVector Toward = (Waypoint - Location).GetSafeNormal2D();
		if (IsPathClear(Location, Toward, static_cast<float>(FVector::Dist2D(Waypoint, Location))))
		{
			Direction = Toward;
			break;
		}
	}
	RouteDirection = Direction.IsNearlyZero() ? Straight : Direction;
	RouteDirectionAt = Now;
	RouteDirectionGoal = Goal;
	return RouteDirection;
}

int32 AChaosImpactCPUController::GetPoints(const AChaosImpactCharacter* Character)
{
	const AChaosImpactPlayerState* State = Character ? Character->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	return State ? State->Points : 0;
}

bool AChaosImpactCPUController::UpdateHunting(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, const float Now)
{
	if (!bPerfect || !Self || !Target || !GetWorld())
	{
		bHunting = false;
		return false;
	}
	// Behind: someone (not on its side) has more points.
	const int32 Mine = GetPoints(Self);
	bool bBehind = false;
	bool bLeading = true;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Self || AChaosImpactGameState::AreTeammates(GetWorld(), *It, Self))
		{
			continue;
		}
		bBehind |= GetPoints(*It) > Mine;
		bLeading &= GetPoints(*It) < Mine;
	}
	// The match nearly over and not ahead.
	bool bClosing = false;
	if (const AChaosImpactGameState* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
		Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match && Match->PhaseEndsAt > 0.0)
	{
		bClosing = !bLeading && Match->PhaseEndsAt - Match->GetServerWorldTimeSeconds() < 45.0;
	}
	// Running from it: moving away briskly, out of close range.
	const FVector Away = (Target->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal2D();
	const float Fleeing = static_cast<float>(FVector::DotProduct(GetObservedVelocity(Target), Away));
	const float Apart = static_cast<float>(FVector::Dist2D(Target->GetActorLocation(), Self->GetActorLocation()));
	const bool bRunning = Fleeing > Target->GetCharacterMovement()->MaxWalkSpeed * 0.45f && Apart > 700.0f;
	// Never lets anyone keep their distance, ahead or not.
	const bool bFar = Apart > 1300.0f;
	if (bBehind || bClosing || bRunning || bFar)
	{
		// Kept up a little while once started, so a runner pausing for a moment is not let off.
		HuntUntil = Now + 2.5f;
	}
	bHunting = Now < HuntUntil;
	return bHunting;
}

bool AChaosImpactCPUController::IsNovaSafe(const AChaosImpactCharacter* Self) const
{
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const AChaosImpactCharacter* Other = *It;
		if (Other == Self || Other->IsEliminated() || AChaosImpactGameState::AreTeammates(GetWorld(), Other, Self))
		{
			continue;
		}
		if (Other->GetCarriedBallCount() > 0 && FVector::Dist2D(Other->GetActorLocation(), Self->GetActorLocation()) < 1700.0f
			&& LineOfSightTo(Other))
		{
			return false;
		}
	}
	return true;
}

void AChaosImpactCPUController::UpdateNovaPerfect(AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, const float Now)
{
	const bool bSafe = IsNovaSafe(Self);
	if (!Self->IsChargingThrow())
	{
		if (!bSafe)
		{
			// Too dangerous to stand still for it: use the other ball meanwhile.
			if (Self->GetCarriedBallCount() >= 2 && Self->GetCarriedBallType(1) != EChaosImpactBallType::Nova)
			{
				Self->RequestBallSwap();
			}
			return;
		}
		if (Now >= NextThrowAt && !Self->IsDashing() && !Self->IsThrowReleasePending())
		{
			Self->BeginThrowInput();
			ChargeStartedAt = Now;
		}
		return;
	}
	if (!bSafe)
	{
		Self->CancelChargingThrow();
		NextThrowAt = Now + 0.3f;
		return;
	}
	const float Alpha = Self->GetThrowChargeAlpha();
	// Aimed at where they will be when it comes down.
	const FVector Toward = PredictTargetAt(Target, 1.6f) - Self->GetActorLocation();
	Self->SetAIAimDirection(Toward.GetSafeNormal2D());
	FVector Landing;
	float Radius = 0.0f;
	if (!Self->PredictThrowLanding(Alpha, Landing, Radius))
	{
		return;
	}
	const float Flight = static_cast<float>(FVector::Dist2D(Self->GetActorLocation(), Landing)) / ChaosImpactBallTypes::NovaThrowSpeed
		+ Self->GetThrowReleaseDelay();
	const FVector There = PredictTargetAt(Target, Flight);
	const float Need = Radius + Target->GetCapsuleComponent()->GetScaledCapsuleRadius() - static_cast<float>(FVector::Dist2D(Landing, There));
	const float Reach = Target->GetCharacterMovement()->MaxWalkSpeed * FMath::Max(0.0f, Flight - SureShotReactionSeconds)
		+ (Target->GetDashReadyInSeconds() < Flight ? Target->GetDashDistance() : 0.0f);
	const float Charged = Now - ChargeStartedAt;
	// Let go once they cannot get out from under it; after a long full charge, whenever they are under it at all.
	if (Need > Reach + 20.0f || (Alpha >= 1.0f && Charged > ChaosImpactBallTypes::NovaChargeSeconds + 2.0f && Need > 0.0f))
	{
		Self->EndThrowInput();
		NextThrowAt = Now + 0.3f;
	}
	else if (Charged > ChaosImpactBallTypes::NovaChargeSeconds + 5.0f)
	{
		Self->CancelChargingThrow();
		NextThrowAt = Now + 0.5f;
	}
}

float AChaosImpactCPUController::GetHazardAt(const AChaosImpactCharacter* Self, const FVector& Point) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const float Body = Self->GetCapsuleComponent()->GetScaledCapsuleRadius();
	float Danger = 0.0f;
	for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
	{
		const AChaosImpactHazardZone* Zone = *It;
		if (Zone->GetSourcePawn() == Self || AChaosImpactGameState::AreTeammates(World, Zone->GetSourcePawn(), Self)
			|| Zone->GetAge() > Zone->GetActiveSeconds())
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Point, Zone->GetActorLocation()));
		const float Reach = Zone->GetRadius() + Body;
		switch (Zone->GetZoneType())
		{
		case EChaosImpactBallType::Fire:
			Danger += Distance < Reach + 50.0f ? 3.0f : 0.0f;
			break;
		case EChaosImpactBallType::Black:
			// Its pull is faster than walking: inside it at all means dashing out.
			Danger += Distance < AChaosImpactHazardZone::BlackHoleRadius + Body
				? 3.0f + (Distance < AChaosImpactHazardZone::BlackHoleBurnRadius + 120.0f ? 4.0f : 0.0f) : 0.0f;
			break;
		case EChaosImpactBallType::Smoke:
			Danger += Distance < Reach + 40.0f ? 2.0f : 0.0f;
			break;
		case EChaosImpactBallType::Ice:
			Danger += Distance < Reach ? 0.6f : 0.0f;
			break;
		default:
			break;
		}
	}
	for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
	{
		const AChaosImpactTornado* Tornado = *It;
		if (!Tornado->IsActive() || Tornado->GetSourcePawn() == Self
			|| AChaosImpactGameState::AreTeammates(World, Tornado->GetSourcePawn(), Self))
		{
			continue;
		}
		// Where it is and where it is going.
		for (const float Ahead : {0.0f, 0.5f, 1.0f})
		{
			const FVector Center = Tornado->GetCenter() + Tornado->GetTravelDirection() * AChaosImpactTornado::TravelSpeed * Ahead;
			Danger += FVector::Dist2D(Point, Center) < AChaosImpactTornado::CatchRadius + Body + 160.0f ? 3.0f : 0.0f;
		}
	}
	// A nova being charged: where it will come down, as big as it may yet grow.
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		const AChaosImpactCharacter* Other = *It;
		float Seconds = 0.0f;
		if (Other == Self || !Other->IsChargingNova() || AChaosImpactGameState::AreTeammates(World, Other, Self)
			|| !Other->GetPresentedCharge(Seconds))
		{
			continue;
		}
		FVector Landing;
		float Radius = 0.0f;
		if (Other->PredictThrowLanding(FMath::Min(Seconds / ChaosImpactBallTypes::NovaChargeSeconds + 0.3f, 1.0f), Landing, Radius)
			&& FVector::Dist2D(Point, Landing) < Radius + Body + 150.0f)
		{
			Danger += 5.0f;
		}
	}
	// Off the stage's edge.
	FHitResult Floor;
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUFloor), false, Self);
	if (!World->LineTraceSingleByObjectType(Floor, Point + FVector(0.0f, 0.0f, 60.0f), Point - FVector(0.0f, 0.0f, 500.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Parameters))
	{
		Danger += 8.0f;
	}
	return Danger;
}

FVector AChaosImpactCPUController::ChooseSafeDirection(const AChaosImpactCharacter* Self, const FVector& Desired) const
{
	const FVector Location = Self->GetActorLocation();
	const float Here = GetHazardAt(Self, Location);
	const FVector Wanted = Desired.GetSafeNormal2D();
	if (Here <= 0.0f && CountArmedThreats(Self) < 2 && (Wanted.IsNearlyZero()
		|| GetHazardAt(Self, Location + Wanted * FMath::Min(GetFreeTravel(Location, Wanted, 300.0f), 280.0f)) <= 0.0f))
	{
		return Desired;
	}
	FVector Best = Wanted;
	float BestScore = -TNumericLimits<float>::Max();
	// Outnumbered, the spot itself matters: as few armed opponents with a clear line to it as can be.
	const bool bOutnumbered = CountArmedThreats(Self) >= 2;
	for (int32 Candidate = 0; Candidate < 16; ++Candidate)
	{
		const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 22.5f, FVector::UpVector);
		const float Free = GetFreeTravel(Location, Direction, 320.0f);
		const FVector Spot = Location + Direction * FMath::Min(Free, 280.0f);
		const float Ahead = GetHazardAt(Self, Spot) + (bOutnumbered ? GetExposureAt(Self, Spot) : 0.0f);
		// Outnumbered, getting behind cover counts for more than the way it wanted to go.
		const float Score = (Wanted.IsNearlyZero() ? 0.0f : static_cast<float>(FVector::DotProduct(Direction, Wanted)) * 1.2f)
			- Ahead * (bOutnumbered ? 2.4f : 1.6f) - (Free < 120.0f ? 0.8f : 0.0f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Direction;
		}
	}
	return Best;
}

const AChaosImpactCPUController::FOpponentModel* AChaosImpactCPUController::FindModel(const AChaosImpactCharacter* Target) const
{
	return Models.Find(TWeakObjectPtr<AChaosImpactCharacter>(const_cast<AChaosImpactCharacter*>(Target)));
}

void AChaosImpactCPUController::CollectActiveBurns(const AChaosImpactCharacter* Self)
{
	ActiveBurns.Reset();
	for (TActorIterator<AChaosImpactHazardZone> It(GetWorld()); It; ++It)
	{
		const AChaosImpactHazardZone* Zone = *It;
		if (Zone->GetSourcePawn() == Self || AChaosImpactGameState::AreTeammates(GetWorld(), Zone->GetSourcePawn(), Self)
			|| Zone->GetAge() > Zone->GetActiveSeconds())
		{
			continue;
		}
		const FVector Where = Zone->GetActorLocation();
		if (Zone->GetZoneType() == EChaosImpactBallType::Fire)
		{
			ActiveBurns.Add(FVector4(Where.X, Where.Y, Where.Z, Zone->GetRadius() + LearnedBurstMargin * 0.3f));
		}
		else if (Zone->GetZoneType() == EChaosImpactBallType::Black)
		{
			ActiveBurns.Add(FVector4(Where.X, Where.Y, Where.Z, AChaosImpactHazardZone::BlackHoleBurnRadius + 40.0f));
		}
	}
}

int32 AChaosImpactCPUController::CountArmedThreats(const AChaosImpactCharacter* Self) const
{
	int32 Count = 0;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const AChaosImpactCharacter* Other = *It;
		if (Other != Self && !Other->IsEliminated() && Other->GetCarriedBallCount() > 0
			&& !AChaosImpactGameState::AreTeammates(GetWorld(), Other, Self)
			&& FVector::Dist2D(Other->GetActorLocation(), Self->GetActorLocation()) < 1600.0f && LineOfSightTo(Other))
		{
			++Count;
		}
	}
	return Count;
}

float AChaosImpactCPUController::GetExposureAt(const AChaosImpactCharacter* Self, const FVector& Point) const
{
	float Exposure = 0.0f;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const AChaosImpactCharacter* Other = *It;
		if (Other == Self || Other->IsEliminated() || Other->GetCarriedBallCount() <= 0
			|| AChaosImpactGameState::AreTeammates(GetWorld(), Other, Self))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Other->GetActorLocation(), Point));
		if (Distance > 1800.0f)
		{
			continue;
		}
		FHitResult Hit;
		FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUExposure), false, Self);
		Parameters.AddIgnoredActor(Other);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Other->GetActorLocation(), Point + FVector(0.0f, 0.0f, 20.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Parameters))
		{
			// Behind cover from this one.
			continue;
		}
		Exposure += 1.2f * (1.0f - Distance / 1800.0f);
	}
	return Exposure;
}

float AChaosImpactCPUController::GetSafeMargin() const
{
	return EvasionSafeMargin + (bPerfect ? LearnedDodgeMargin : 0.0f);
}

void AChaosImpactCPUController::NoteThrowAt(const AChaosImpactCharacter* Target, const FVector& Direction, const float Now)
{
	if (!Target)
	{
		return;
	}
	FOpponentModel& Model = Models.FindOrAdd(const_cast<AChaosImpactCharacter*>(Target));
	Model.WatchSince = Now;
	Model.WatchVelocity = Target->GetVelocity();
	Model.WatchSide = FVector::CrossProduct(FVector::UpVector, Direction.GetSafeNormal2D());
}

void AChaosImpactCPUController::UpdateLearning(const AChaosImpactCharacter* Self, const float Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Other = *It;
		if (Other == Self || AChaosImpactGameState::AreTeammates(World, Other, Self))
		{
			continue;
		}
		FOpponentModel& Model = Models.FindOrAdd(Other);
		if (Other->IsEliminated())
		{
			Model.Predictions.Reset();
			Model.WatchSince = -1.0;
			continue;
		}
		// How far off reading its movement turns out: a look-ahead now, checked when the time comes.
		for (int32 Index = Model.Predictions.Num() - 1; Index >= 0; --Index)
		{
			if (Now >= Model.Predictions[Index].Key)
			{
				const float Error = static_cast<float>(FVector::Dist2D(Model.Predictions[Index].Value, Other->GetActorLocation()));
				// A respawn is not a move.
				if (Error < 1500.0f)
				{
					Model.PredictionError = FMath::Lerp(Model.PredictionError, Error, 0.08f);
				}
				Model.Predictions.RemoveAt(Index);
			}
		}
		if (Now >= Model.NextPredictionAt && Model.Predictions.Num() < 8)
		{
			Model.Predictions.Add(TPair<double, FVector>(Now + 0.4, PredictTargetAt(Other, 0.4f)));
			Model.NextPredictionAt = Now + 0.1;
		}
		// Answering a throw: how soon it moved, and whether with a dash.
		if (Model.WatchSince >= 0.0)
		{
			const float Since = static_cast<float>(Now - Model.WatchSince);
			const FVector Velocity = Other->GetVelocity();
			const bool bDashed = Other->IsDashing();
			// Getting out of its way means moving across the throw: a sidestep that was not there before it came.
			const float Across = static_cast<float>(FMath::Abs(FVector::DotProduct(Velocity, Model.WatchSide)
				- FVector::DotProduct(Model.WatchVelocity, Model.WatchSide)));
			const bool bSidestepped = Across > 300.0f;
			if (Since >= 0.06f && (bDashed || bSidestepped))
			{
				// Quicker than thought counts at once; slower only a little at a time. (Nobody answers within a tenth
				// of a second: faster than that was a step already under way.)
				Model.ReactionSeconds = FMath::Clamp(FMath::Lerp(Model.ReactionSeconds, Since, Since < Model.ReactionSeconds ? 0.4f : 0.12f),
					0.1f, 0.4f);
				Model.DashAnswerRate = FMath::Lerp(Model.DashAnswerRate, bDashed ? 1.0f : 0.0f, 0.25f);
				++Model.ThrowsSeen;
				Model.WatchSince = -1.0;
				UE_LOG(LogTemp, Log, TEXT("PERFECTCPU %s reads %s: reacts in %.2fs, dashes %.0f%%, moves %.0fcm off a 0.4s read"),
					*GetName(), *Other->GetName(), Model.ReactionSeconds, Model.DashAnswerRate * 100.0f, Model.PredictionError);
			}
			else if (Since > 0.9f)
			{
				// It did nothing at all about it.
				Model.DashAnswerRate = FMath::Lerp(Model.DashAnswerRate, 0.0f, 0.25f);
				++Model.ThrowsSeen;
				Model.WatchSince = -1.0;
			}
		}
	}

	// Something got through: learn from what it was.
	const float Health = Self->GetHealth();
	if (!Self->IsEliminated() && LastSelfHealth > 0.0f && Health < LastSelfHealth && Now - LastLearnedHitAt > 0.2)
	{
		LastLearnedHitAt = Now;
		++HitsTaken;
		const AActor* Cause = Self->GetLastDamageCauser();
		const AChaosImpactBall* Ball = Cast<AChaosImpactBall>(Cause);
		const AChaosImpactHazardZone* Zone = Cast<AChaosImpactHazardZone>(Cause);
		if (Zone)
		{
			// Caught in a blast or on burning ground: give blasts and hazards more room from now on.
			LearnedBurstMargin = FMath::Min(LearnedBurstMargin + 30.0f, 120.0f);
		}
		else
		{
			// A ball got through: dodge wider, dash sooner, look further ahead.
			LearnedDodgeMargin = FMath::Min(LearnedDodgeMargin + 8.0f, 32.0f);
			LearnedDashLead = FMath::Min(LearnedDashLead + 0.02f, 0.06f);
			LearnedHorizonBonus = FMath::Min(LearnedHorizonBonus + 0.15f, 0.6f);
		}
		UE_LOG(LogTemp, Log, TEXT("PERFECTCPU %s learned from a hit by %s (%s): dodge +%.0f, dash lead +%.2f, blast +%.0f"),
			*GetName(), Ball ? ChaosImpactBallTypes::GetInternalName(Ball->GetBallType())
				: Zone ? ChaosImpactBallTypes::GetInternalName(Zone->GetZoneType()) : TEXT("something"),
			Zone ? TEXT("blast") : TEXT("ball"), LearnedDodgeMargin, LearnedDashLead, LearnedBurstMargin);
	}
	LastSelfHealth = Self->IsEliminated() ? -1.0f : Health;
}

bool AChaosImpactCPUController::HasClearShotFrom(const FVector& Point, const AChaosImpactCharacter* Self,
	const AChaosImpactCharacter* Target) const
{
	if (!GetWorld() || !Self || !Target)
	{
		return false;
	}
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUClearShot), false, Self);
	Parameters.AddIgnoredActor(Target);
	const FVector From(Point.X, Point.Y, Point.Z - Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + LearnedReleaseHeight);
	FHitResult Hit;
	return !GetWorld()->SweepSingleByObjectType(Hit, From, Target->GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(BallRadius), Parameters);
}

bool AChaosImpactCPUController::FindVantagePoint(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target,
	const float Ideal, FVector& OutPoint) const
{
	const FVector Location = Self->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	float BestScore = TNumericLimits<float>::Max();
	bool bFound = false;
	// Points it can walk straight to (nothing in the way), on the floor, out of hazards.
	const auto Reachable = [this, Self](const FVector& From, const FVector& Direction, const float Distance, FVector& OutWhere)
	{
		if (GetFreeTravel(From, Direction, Distance) < Distance * 0.97f)
		{
			return false;
		}
		OutWhere = From + Direction * Distance;
		return GetHazardAt(Self, OutWhere) < 1.0f;
	};
	static constexpr float Rings[] = {250.0f, 500.0f, 800.0f, 1100.0f};
	for (const float Ring : Rings)
	{
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Step * 22.5f, FVector::UpVector);
			FVector Point;
			if (!Reachable(Location, Direction, Ring, Point) || !HasClearShotFrom(Point, Self, Target))
			{
				continue;
			}
			const float Score = Ring + FMath::Abs(static_cast<float>(FVector::Dist2D(Point, TargetLocation)) - Ideal) * 0.6f;
			if (Score < BestScore)
			{
				BestScore = Score;
				OutPoint = Point;
				bFound = true;
			}
		}
	}
	if (bFound)
	{
		return true;
	}
	// Nothing straight ahead: two legs, round the corner; head for the first.
	for (const float Ring : {400.0f, 800.0f})
	{
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Step * 22.5f, FVector::UpVector);
			FVector Corner;
			if (!Reachable(Location, Direction, Ring, Corner))
			{
				continue;
			}
			for (int32 Turn = 0; Turn < 12; ++Turn)
			{
				const FVector Onward = FVector::ForwardVector.RotateAngleAxis(Turn * 30.0f, FVector::UpVector);
				for (const float Leg : {400.0f, 800.0f})
				{
					FVector Point;
					if (!Reachable(Corner, Onward, Leg, Point) || !HasClearShotFrom(Point, Self, Target))
					{
						continue;
					}
					const float Score = Ring + Leg + FMath::Abs(static_cast<float>(FVector::Dist2D(Point, TargetLocation)) - Ideal) * 0.6f;
					if (Score < BestScore)
					{
						BestScore = Score;
						OutPoint = Corner;
						bFound = true;
					}
				}
			}
		}
	}
	return bFound;
}

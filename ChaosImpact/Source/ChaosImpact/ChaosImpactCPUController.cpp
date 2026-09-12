#include "ChaosImpactCPUController.h"

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AChaosImpactCPUController::AChaosImpactCPUController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
}

void AChaosImpactCPUController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	NextDecisionAt = Now + 0.25f;
	NextThrowAt = Now + 1.2f;
	NextDashAt = Now + 1.0f;
	NextStrafeChangeAt = Now + 1.5f;
	NextJumpAt = Now + FMath::FRandRange(2.0f, 3.5f);
	StrafeSign = FMath::FRand() < 0.5f ? -1.0f : 1.0f;
}

void AChaosImpactCPUController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AChaosImpactCharacter* ControlledCharacter = Cast<AChaosImpactCharacter>(GetPawn());
	if (!ControlledCharacter || ControlledCharacter->IsEliminated() || !GetWorld())
	{
		bChargingThrow = false;
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (bHoldingJump && Now >= StopJumpAt)
	{
		ControlledCharacter->StopJumping();
		bHoldingJump = false;
	}
	if (bChargingThrow && Now >= ReleaseThrowAt)
	{
		ControlledCharacter->EndThrowInput();
		bChargingThrow = false;
		NextThrowAt = Now + FMath::FRandRange(0.8f, 1.45f);
	}

	if (Now >= NextDecisionAt)
	{
		NextDecisionAt = Now + 0.07f;
		RefreshDecision(ControlledCharacter, Now);
	}

	// Decisions are throttled, but movement input must be supplied every frame.
	// Otherwise CharacterMovement consumes it and the CPU visibly stutters.
	if (!ControlledCharacter->IsDashing() && !DesiredMoveDirection.IsNearlyZero())
	{
		ControlledCharacter->AddMovementInput(DesiredMoveDirection, DesiredMoveScale);
	}
}

void AChaosImpactCPUController::RefreshDecision(
	AChaosImpactCharacter* ControlledCharacter, const float Now)
{
	if (TryDodgeIncomingBall(ControlledCharacter, Now))
	{
		DesiredMoveDirection = FVector::ZeroVector;
		DesiredMoveScale = 0.0f;
		return;
	}

	const FVector Location = ControlledCharacter->GetActorLocation();
	if (ControlledCharacter->GetCarriedBallCount() <= 0)
	{
		if (AChaosImpactBall* Pickup = FindNearestPickup(Location))
		{
			const float PickupDistance = FVector::Dist2D(Pickup->GetActorLocation(), Location);
			if (PickupDistance <= 108.0f && Pickup->IsPickupAvailable()
				&& ControlledCharacter->TryPickupBall(Pickup))
			{
				Pickup->Destroy();
				DesiredMoveDirection = FVector::ZeroVector;
				DesiredMoveScale = 0.0f;
				NextThrowAt = Now + FMath::FRandRange(0.22f, 0.48f);
				return;
			}

			const float LeadSeconds = FMath::Clamp(PickupDistance / 1800.0f, 0.05f, 0.28f);
			const FVector PredictedPickup = Pickup->GetActorLocation()
				+ Pickup->GetBallVelocity() * LeadSeconds;
			const FVector ToPickup = (PredictedPickup - Location).GetSafeNormal2D();
			ControlledCharacter->SetAIAimDirection(ToPickup);
			const FVector AvoidedDirection = AvoidNearbyObstacle(Location, ToPickup);
			DesiredMoveDirection = FMath::VInterpTo(DesiredMoveDirection,
				AvoidedDirection, 0.07f, 8.5f).GetSafeNormal2D();
			DesiredMoveScale = FMath::Clamp(PickupDistance / 260.0f, 0.38f, 1.0f);
		}
		else
		{
			DesiredMoveDirection = FVector::ZeroVector;
			DesiredMoveScale = 0.0f;
		}
		return;
	}

	AChaosImpactCharacter* Opponent = FindNearestOpponent(Location);
	if (!Opponent)
	{
		DesiredMoveDirection = FVector::ZeroVector;
		DesiredMoveScale = 0.0f;
		return;
	}
	const FVector ToOpponent = Opponent->GetActorLocation() - Location;
	const float Distance = ToOpponent.Size2D();
	const float LeadSeconds = FMath::Clamp(Distance / 2700.0f, 0.08f, 0.46f);
	const FVector PredictedOpponent = Opponent->GetActorLocation()
		+ Opponent->GetVelocity() * LeadSeconds;
	const FVector AimDirection = (PredictedOpponent - Location).GetSafeNormal2D();
	ControlledCharacter->SetAIAimDirection(AimDirection);

	if (Now >= NextStrafeChangeAt)
	{
		StrafeSign *= -1.0f;
		NextStrafeChangeAt = Now + FMath::FRandRange(1.25f, 2.5f);
	}
	const FVector Strafe = FVector::CrossProduct(FVector::UpVector, AimDirection) * StrafeSign;
	const FVector RangeCorrection = Distance > 1250.0f ? AimDirection * 0.95f
		: Distance < 560.0f ? -AimDirection : AimDirection * 0.12f;
	const FVector CombatDirection = (RangeCorrection + Strafe * 0.78f).GetSafeNormal2D();
	const FVector AvoidedDirection = AvoidNearbyObstacle(Location, CombatDirection);
	DesiredMoveDirection = FMath::VInterpTo(DesiredMoveDirection,
		AvoidedDirection, 0.07f, 6.5f).GetSafeNormal2D();
	DesiredMoveScale = 0.92f;
	if (Now >= NextJumpAt && !ControlledCharacter->IsDashing()
		&& (HasLowObstacleAhead(Location, CombatDirection) || Distance < 1450.0f))
	{
		ControlledCharacter->Jump();
		bHoldingJump = true;
		StopJumpAt = Now + 0.14f;
		NextJumpAt = Now + FMath::FRandRange(3.6f, 6.2f);
	}

	if (!bChargingThrow && Now >= NextThrowAt && Distance < 2700.0f)
	{
		ControlledCharacter->BeginThrowInput();
		bChargingThrow = ControlledCharacter->IsChargingThrow();
		ReleaseThrowAt = Now + FMath::FRandRange(0.78f, 1.32f);
	}
	else if (Now >= NextDashAt && Distance < 690.0f)
	{
		ControlledCharacter->RequestAIDash((Strafe - AimDirection * 0.18f).GetSafeNormal2D());
		NextDashAt = Now + FMath::FRandRange(1.45f, 2.5f);
	}
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

FVector AChaosImpactCPUController::AvoidNearbyObstacle(
	const FVector& Origin, const FVector& DesiredDirection) const
{
	if (!GetWorld() || DesiredDirection.IsNearlyZero())
	{
		return DesiredDirection;
	}
	FHitResult Hit;
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(ChaosImpactCPUObstacle), false, GetPawn());
	const FVector TraceStart = Origin + FVector::UpVector * 45.0f;
	const FVector TraceEnd = TraceStart + DesiredDirection * 185.0f;
	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Parameters)
		&& Hit.bBlockingHit)
	{
		FVector Slide = DesiredDirection
			- Hit.ImpactNormal * FVector::DotProduct(DesiredDirection, Hit.ImpactNormal);
		if (Slide.IsNearlyZero())
		{
			Slide = FVector::CrossProduct(FVector::UpVector, DesiredDirection) * StrafeSign;
		}
		return Slide.GetSafeNormal2D();
	}
	return DesiredDirection;
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

AChaosImpactBall* AChaosImpactCPUController::FindNearestPickup(const FVector& From) const
{
	AChaosImpactBall* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		if (!It->IsPickup())
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(From, It->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = *It;
		}
	}
	return Best;
}

AChaosImpactCharacter* AChaosImpactCPUController::FindNearestOpponent(const FVector& From) const
{
	AChaosImpactCharacter* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == GetPawn() || It->IsEliminated())
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(From, It->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = *It;
		}
	}
	return Best;
}

bool AChaosImpactCPUController::TryDodgeIncomingBall(
	AChaosImpactCharacter* ControlledCharacter, const float Now)
{
	if (Now < NextDashAt)
	{
		return false;
	}
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		if (Ball->IsPickup() || Ball->WasThrownBy(ControlledCharacter))
		{
			continue;
		}
		const FVector BallToCPU = ControlledCharacter->GetActorLocation() - Ball->GetActorLocation();
		if (BallToCPU.SizeSquared2D() > FMath::Square(760.0f))
		{
			continue;
		}
		const FVector Velocity = Ball->GetBallVelocity().GetSafeNormal2D();
		if (!Velocity.IsNearlyZero() && FVector::DotProduct(Velocity, BallToCPU.GetSafeNormal2D()) > 0.25f)
		{
			FVector DodgeDirection = FVector::CrossProduct(FVector::UpVector, Velocity).GetSafeNormal2D();
			if (FMath::FRand() < 0.5f)
			{
				DodgeDirection *= -1.0f;
			}
			ControlledCharacter->RequestAIDash(DodgeDirection);
			NextDashAt = Now + FMath::FRandRange(1.35f, 2.3f);
			return true;
		}
	}
	return false;
}

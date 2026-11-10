#include "BRGame.h"
#include "BRIsland.h"
#include "EngineUtils.h"

// CPUs that have become real characters because someone is near. Simple and watchable rather than the VS CPU's full
// simulation: they grab balls, keep to the circle, throw with a human's aim error and sometimes dodge.

ABRBotController::ABRBotController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bWantsPlayerState = true;
}

ABRCharacter* ABRBotController::FindEnemy(ABRCharacter* Self, const float Range, bool& bOutVisible) const
{
	ABRCharacter* Best = nullptr;
	double BestD = FMath::Square(static_cast<double>(Range));
	bOutVisible = false;
	for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
	{
		ABRCharacter* Other = *It;
		if (Other == Self || Other->TeamId == Self->TeamId || Other->IsEliminated() || Other->IsFlying()) { continue; }
		const double D = FVector::DistSquared2D(Other->GetActorLocation(), Self->GetActorLocation());
		if (D < BestD) { BestD = D; Best = Other; }
	}
	if (Best)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BRBotSight), false, Self);
		Params.AddIgnoredActor(Best);
		FHitResult Hit;
		bOutVisible = !GetWorld()->LineTraceSingleByChannel(Hit, Self->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f), Best->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f), ECC_WorldStatic, Params);
	}
	return Best;
}

AChaosImpactBall* ABRBotController::FindBallPickup(ABRCharacter* Self, const float Range) const
{
	AChaosImpactBall* Best = nullptr;
	double BestD = FMath::Square(static_cast<double>(Range));
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		if (!It->IsPickupAvailable()) { continue; }
		const double D = FVector::DistSquared2D(It->GetActorLocation(), Self->GetActorLocation());
		if (D < BestD) { BestD = D; Best = *It; }
	}
	return Best;
}

bool ABRBotController::DodgeIncoming(ABRCharacter* Self, const double Now)
{
	if (Now < NextDashAt || !Self->CanDashNow()) { return false; }
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		const AChaosImpactBall* Ball = *It;
		if (Ball->IsPickup() || !Ball->GetThrowingPawn()) { continue; }
		const ABRCharacter* Thrower = Cast<ABRCharacter>(Ball->GetThrowingPawn());
		if (Thrower && Thrower->TeamId == Self->TeamId) { continue; }
		const FVector Velocity = Ball->GetVelocity();
		const FVector To = Self->GetActorLocation() - Ball->GetActorLocation();
		if (To.SizeSquared2D() > FMath::Square(750.0f) || FVector::DotProduct(Velocity.GetSafeNormal2D(), To.GetSafeNormal2D()) < 0.92f) { continue; }
		// Only a good CPU reads it in time, most of them.
		NextDashAt = Now + 1.2;
		if (FMath::FRand() < 0.15f + Skill * 0.6f)
		{
			Self->RequestAIDash(FVector::CrossProduct(FVector::UpVector, Velocity.GetSafeNormal2D()) * (FMath::RandBool() ? 1.0f : -1.0f));
			return true;
		}
		return false;
	}
	return false;
}

void ABRBotController::MoveToward(ABRCharacter* Self, const FVector& Where, const float Rate, const double Now)
{
	FVector Want = (Where - Self->GetActorLocation()).GetSafeNormal2D();
	if (Want.IsNearlyZero()) { return; }
	FVector Move = Now < AvoidUntil ? AvoidDirection : Want;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BRBotMove), false, Self);
	for (const float Angle : {0.0f, 40.0f, -40.0f, 85.0f, -85.0f, 135.0f, -135.0f})
	{
		const FVector Candidate = Move.RotateAngleAxis(Angle * StrafeSign, FVector::UpVector).GetSafeNormal2D();
		FHitResult Wall;
		const FVector From = Self->GetActorLocation();
		if (!GetWorld()->SweepSingleByObjectType(Wall, From, From + Candidate * 170.0f, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(32.0f), Params)
			|| Wall.ImpactNormal.Z > 0.6f)
		{
			if (Angle != 0.0f)
			{
				AvoidDirection = Candidate;
				AvoidUntil = Now + 0.5;
			}
			Self->AddMovementInput(Candidate, Rate);
			break;
		}
	}
	if (FVector::DistSquared2D(LastLocation, Self->GetActorLocation()) < 4.0 && Now >= AvoidUntil)
	{
		AvoidDirection = -Want.RotateAngleAxis(FMath::FRandRange(-60.0f, 60.0f), FVector::UpVector);
		AvoidUntil = Now + 0.8;
		StrafeSign = -StrafeSign;
	}
	LastLocation = Self->GetActorLocation();
}

void ABRBotController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABRCharacter* Self = Cast<ABRCharacter>(GetPawn());
	ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>();
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (Self && Mode && Self->IsFreefalling())
	{
		// Steer for where the team is going.
		const FVector To = Mode->GetBotLanding(Self) - Self->GetActorLocation();
		if (To.SizeSquared2D() > FMath::Square(500.0f)) { Self->AddMovementInput(To.GetSafeNormal2D(), 1.0f); }
		return;
	}
	if (!Self || !Mode || !State || (State->Phase != EBRPhase::Play && State->Phase != EBRPhase::Ship) || Self->IsEliminated() || Self->IsDowned() || Self->IsAboard()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (Self->IsChargingThrow())
	{
		if (Now >= ReleaseAt)
		{
			Self->EndThrowInput();
			NextThrowAt = Now + FMath::FRandRange(1.0f, 2.6f) * (1.4f - Skill * 0.7f);
		}
		return;
	}

	// A teammate down within reach comes first.
	if (ABRCharacter* Down = [&]() -> ABRCharacter*
	{
		for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != Self && It->TeamId == Self->TeamId && It->IsDowned() && FVector::DistSquared2D(It->GetActorLocation(), Self->GetActorLocation()) < FMath::Square(2800.0f)) { return *It; }
		}
		return nullptr;
	}())
	{
		if (Self->CanRevive(Down) && Down->ReviveEndsAt <= 0.0) { Self->InteractNow(); }
		else if (FVector::DistSquared2D(Down->GetActorLocation(), Self->GetActorLocation()) > FMath::Square(150.0f)) { MoveToward(Self, Down->GetActorLocation(), 1.0f, Now); }
		return;
	}

	// Heal up when hurt and nobody is close.
	bool bVisible = false;
	ABRCharacter* Enemy = FindEnemy(Self, 2600.0f, bVisible);
	if (!Enemy && Self->GetHealthNow() < 60.0f && Self->Heals > 0 && Self->HealEndsAt <= 0.0) { Self->DoUseHeal(); }
	DodgeIncoming(Self, Now);

	const bool bNeedBall = Self->GetCarriedBallCount() < Self->GetMaximumCarriedBalls();
	AChaosImpactBall* Ball = bNeedBall ? FindBallPickup(Self, Enemy ? 900.0f : 2400.0f) : nullptr;

	if (Enemy)
	{
		const FVector To = Enemy->GetActorLocation() - Self->GetActorLocation();
		const float Distance = static_cast<float>(To.Size2D());
		const FVector Toward = To.GetSafeNormal2D();
		if (Self->GetCarriedBallCount() > 0 && bVisible && Distance < 1700.0f && Now >= NextThrowAt && !Self->IsThrowReleasePending())
		{
			// A little ahead of where it is going, and a very human error in the aim.
			const float Lead = Distance / 2200.0f;
			const FVector Aim = (To + Enemy->GetVelocity() * Lead * Skill).GetSafeNormal2D();
			const float Error = FMath::Lerp(16.0f, 2.5f, Skill);
			Self->SetAIAimDirection(Aim.RotateAngleAxis(FMath::FRandRange(-Error, Error), FVector::UpVector));
			Self->BeginThrowInput();
			ReleaseAt = Now + FMath::FRandRange(0.15f, 0.25f + Skill * 0.75f);
			return;
		}
		Self->SetAIAimDirection(Toward);
		if (Ball) { MoveToward(Self, Ball->GetActorLocation(), 1.0f, Now); return; }
		if (Now >= NextWanderAt) { StrafeSign = FMath::RandBool() ? 1.0f : -1.0f; NextWanderAt = Now + FMath::FRandRange(0.9f, 2.0f); }
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward) * StrafeSign;
		if (Self->GetCarriedBallCount() == 0) { MoveToward(Self, Self->GetActorLocation() - Toward * 600.0f + Side * 300.0f, 1.0f, Now); }
		else if (Distance > 1300.0f || !bVisible) { MoveToward(Self, Enemy->GetActorLocation() + Side * 200.0f, 1.0f, Now); }
		else if (Distance < 520.0f) { MoveToward(Self, Self->GetActorLocation() - Toward * 500.0f + Side * 300.0f, 1.0f, Now); }
		else { MoveToward(Self, Self->GetActorLocation() + Side * 500.0f, 0.8f, Now); }
		return;
	}
	if (Ball) { MoveToward(Self, Ball->GetActorLocation(), 0.9f, Now); return; }

	// Stay with a person on the team (or the nearest teammate); otherwise make for the circle.
	if (const ABRCharacter* Ally = Mode->FindNearestAlly(Self))
	{
		if (Ally->GetController() && Ally->GetController()->IsPlayerController())
		{
			if (FVector::DistSquared2D(Ally->GetActorLocation(), Self->GetActorLocation()) > FMath::Square(620.0f))
			{
				MoveToward(Self, Ally->GetActorLocation(), 1.0f, Now);
			}
			return;
		}
	}
	FVector2D Centre;
	float Radius;
	State->GetZoneNow(Centre, Radius);
	const FVector Goal = Mode->GetBotGoal(Self);
	const bool bHurry = FVector2D::Distance(FVector2D(Self->GetActorLocation()), Centre) > Radius * 0.9f;
	if (FVector::DistSquared2D(Goal, Self->GetActorLocation()) > FMath::Square(250.0f))
	{
		MoveToward(Self, Goal, bHurry ? 1.0f : 0.72f, Now);
		Self->SetAIAimDirection((Goal - Self->GetActorLocation()).GetSafeNormal2D());
	}
}
